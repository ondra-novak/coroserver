#include "socket_stream.h"
#include "buffered_stream.h"

namespace coroserver {

template class BufferedStreamT<StreamProxy>;

SocketStream::SocketStream(std::shared_ptr<INetContext> ctx, ConnHandle h)
:_ctx(std::move(ctx))
,_h(h)
{
    //immediately manifest that we are ready to send
    ready_to_send();
}

void SocketStream::destroy_me() {
    //if destroy is called in callback
    if (_ctx->in_calback()) {
        //destroy outside of callback
        _ctx->enqueue([me = std::unique_ptr<SocketStream>(this)]{});
    } else {
        //destroy now
        delete this;
    }
}

Stream SocketStream::create(std::shared_ptr<INetContext> ctx, ConnHandle h) {
    return std::shared_ptr<IStream>(new SocketStream(std::move(ctx), h),
            [](SocketStream *me){
        me->destroy_me();
    });
}

SocketStream::~SocketStream() {
    _ctx->destroy(_h);
}

void SocketStream::begin_receive() {
    _input_buffer.resize(_input_buffer_size);
    _ctx->receive(_h, _input_buffer, this);
    _receiving = true;
}

void SocketStream::receive_complete(std::string_view data) noexcept {
    coro::prepared_coro p;
    //hold lock when receive complete
    std::lock_guard _(_mx);
    _count_input_bytes += data.size();
    //if buffer has been used fully, it is probably small
    if (data.size() == _input_buffer_size) {
        //increase it
        _input_buffer_size = _input_buffer_size + 1024;
    }
    //if received buffer is empty
    if (data.empty()) {
        //this is eof
        _is_eof = true;
    }
    //if we have promise
    if (_receive_promise) {
        //send data to promise
        p = _receive_promise(data);
    } else {
        //otherwise remember it to return later
        _input_buffer_ready = data;
    }
    //no longe receiving
    _receiving = false;
}

coro::awaitable<std::string_view> SocketStream::receive() {
    //there is no lock as the receive is not marked MT safe
    //if putback buffer or is eof, return buffer
    if (!_input_buffer_ready.empty() || _is_eof) return std::exchange(_input_buffer_ready, {});
    //update timeout
    current_recv_tm = _tms._receive;
    update_timer();
    //otherwise it could be resolved asynchronously
    return [this](coro::awaitable<std::string_view>::result r) {
        std::lock_guard _(_mx);
        //remember promise
        _receive_promise = std::move(r);
        //if not receiving yet, start now
        if (!_receiving) {
            begin_receive();
        }
    };
}

void SocketStream::put_back(std::string_view s) {
    //there is no lock. receive and put_back are not marked MT safe
    _input_buffer_ready = s;
}


/*coro::awaitable<bool> SocketStream::close() {
    std::lock_guard _(_mx);
    if (_output_closed) return false;
    //if clear to send - set eof immediately
    if (_clear_to_send) {
        _ctx->send(_h,{});//SEND EOF
        _output_closed = true;
        return true;
    } else {
        _send_eof = true;   //send asynchronous
        return [this](coro::awaitable<bool>::result r) {
            if (_output_closed || _clear_to_send) {
                r = true;
            } else {
                _awaiting_results.push({_count_output_bytes+_output_buffer.size()+1, std::move(r)});
            }
        };
    }

}
*/
coro::awaitable<bool> SocketStream::close() {
    if (!_output_closed) {
        _ctx->send(_h,{});//SEND EOF;
        _output_closed = true;
        return true;
    }
    return false;
}



coro::awaitable<bool> SocketStream::send(std::string_view data) {
    std::lock_guard _(_mx);
    if (_output_closed) {
        return false;
    }
    if (data.empty()) {
        if (_clear_to_send) {
            return true;
        }
    } else if (_clear_to_send) {
        auto st = _ctx->send(_h, data);
        if (st == SendStatus::broken) {
            _output_closed = true;
            return false;
        }
        _count_output_bytes+=data.size();
        if (st == SendStatus::sync) return true;
        ready_to_send();
    } else {
        _output_view = data;
    }
    _clear_to_send = false;
    return [this](coro::awaitable<bool>::result r) ->coro::prepared_coro {
        std::lock_guard _(_mx);
        if (_output_closed) return r(false);
        if (_clear_to_send) return r(true);
        _awaiting_write = std::move(r);
        return {};
    };
}

void SocketStream::clear_to_send() noexcept {
    coro::prepared_coro out;
    //called when we are clear to send
    std::lock_guard _(_mx);

    if (!_output_view.empty()) {
        _count_output_bytes+=_output_view.size();
        auto st = _ctx->send(_h,std::exchange(_output_view, {}));
        if (st == SendStatus::async) {
            ready_to_send();
            return;
        }
        if (st == SendStatus::broken) {
            _output_closed = true;
        }
    }

    if (!_output_closed) {
        _clear_to_send = true;
    }

    out = _awaiting_write(!_output_closed);
}

IOTimeout SocketStream::get_timeouts() const {
    std::lock_guard _(_mx);
    return _tms;
}

IStream::Counters SocketStream::get_counters() const {
    std::lock_guard _(_mx);
    return {_count_input_bytes, _count_output_bytes};
}

void SocketStream::set_timeouts(coroserver::IOTimeout tm)  {
    std::lock_guard _(_mx);

    bool update_recv = tm._receive!= _tms._receive;
    bool update_send = tm._send != _tms._send;
    _tms = tm;
    if (update_recv) current_recv_tm = _tms._receive;
    if (update_send) current_send_tm = _tms._send;
    update_timer();


}

void SocketStream::update_timer() {
    //select nearest time-point
    auto nearest = std::min(current_recv_tm, current_send_tm);
    //if it is different
    if (current_tm != nearest) {
        //update it
        current_tm = nearest;
        //if it is not max
        if (current_tm != std::chrono::system_clock::time_point::max()) {
            //change timeout
            _ctx->set_timeout(_h, current_tm, this);
        }
    }
}

void SocketStream::on_timeout() noexcept {
    std::lock_guard _(_mx);
    auto now = std::chrono::system_clock::now();
    //timeout from writing
    if (now >= current_send_tm) {
        //we must be waiting on ready_to_send
        if (!_clear_to_send) {
            //close output
            _output_closed = true;
            //notify error
            _awaiting_write(false);
            //reset timeout
            current_send_tm = current_send_tm.max();
        }
    }
    //timeout for reading
    if (now >= current_recv_tm) {
        //resolve awaiting promise with empty string
        _ctx->enqueue(_receive_promise(std::string_view()));
        //reset timeout
        current_recv_tm = current_recv_tm.max();
    }
    update_timer();
}

void SocketStream::ready_to_send() {
    //Initiate waiting for clear to send
    //update send timeout
    current_send_tm = _tms._send;
    //update global timer
    update_timer();
    //clear the clear to send flag
    _clear_to_send = false;
    //request clear to send flag
    _ctx->ready_to_send(_h, this);
}

StreamState SocketStream::get_state() const {
    std::lock_guard _(_mx);
    if (_is_eof) return StreamState::closed;
    if (_output_closed) return StreamState::closing;
    if (_opening_state) return StreamState::opening;
    return StreamState::active;
}


Stream SocketStream::connect(std::shared_ptr<INetContext> ctx, std::string address_port) {
    auto h = ctx->connect(std::move(address_port));
    return create(std::move(ctx), h);
}

Stream SocketStream::connect(std::shared_ptr<INetContext> ctx, SpecialConnection type, const void *arg) {
    auto h = ctx->connect(type, arg);
    return create(std::move(ctx), h);
}

TCPServer::AWT TCPServer::accept_handle() {
    return [this](PROM r) -> coro::prepared_coro{
        AWT *need = nullptr;
        AWT *prom = r.release();
        if (_r.compare_exchange_strong(need, prom)) {
            _ctx->accept(_h, this);
            return {};
        }
        PROM res(prom);
        return res.set_empty();
    };
}

TCPServer::TCPServer(std::shared_ptr<INetContext> ctx, std::string address_port)
    :_ctx(std::move(ctx)), _h(_ctx->create_server(address_port)) {}


void TCPServer::on_accept(ConnHandle connection, std::string peer_addr) noexcept {
    auto r = _r.exchange(nullptr);
    if (r) {
        PROM res(r);
        _ctx->enqueue(res(AcceptInfo{connection, std::move(peer_addr)}));
    } else {
        _ctx->destroy(connection);
    }

}



coro::async_generator<Stream> SocketStream::create_tcp_server(std::shared_ptr<INetContext> ctx, std::string address_port, std::stop_token stp) {
    return create_tcp_server(std::move(ctx), std::move(address_port), std::move(stp),[](auto &&){return true;});
}

TCPServer::~TCPServer() {
    _ctx->destroy(_h);
}

coro::prepared_coro TCPServer::cancel() {
    static AWT filler;
    auto r = _r.exchange(&filler);
    PROM res(r);
    return res.set_empty();
}

coro::awaitable<Stream> TCPServer::accept(std::string &addr_port) {
    auto awt = accept_handle();
    if (awt.await_ready()) {
        auto [h, peer] = awt.await_resume();
        addr_port = std::move(peer);
        return SocketStream::create(_ctx, h);
    } else {
        awt.cancel();
        return [this,&addr_port](coro::awaitable<Stream>::result r) mutable {
            auto awt = accept_handle();
            _accept_cb.await(awt, [this, r = std::move(r), &addr_port](auto &x) mutable{
                do_accept_raw(x, r, &addr_port);
            });
        };
    }
}

void TCPServer::do_accept_raw(coro::awaitable<AcceptInfo> &ainfo, coro::awaitable<Stream>::result &r, std::string * peer) {
    try {
        auto [h, p] = ainfo.await_resume();
        if (peer) *peer = std::move(p);
        r = SocketStream::create(_ctx, h);
    } catch (...) {
        r.set_exception(std::current_exception());
    }
}

coro::awaitable<Stream> TCPServer::accept() {
    auto awt = accept_handle();
    if (awt.await_ready()) {
        auto [h, peer] = awt.await_resume();
        return SocketStream::create(_ctx, h);
    } else {
        awt.cancel();
        return [this](coro::awaitable<Stream>::result r) mutable {
            _accept_cb.await(accept_handle(), [this, r = std::move(r)](auto &x) mutable {
                do_accept_raw(x, r, nullptr);
            });
        };
    }
}

std::shared_ptr<INetContext> SocketStream::get_async_context() const {
    return _ctx;
}

}
