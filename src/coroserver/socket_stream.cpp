#include "socket_stream.h"

namespace coroserver {

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
    prepared_coro p;
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

awaitable<std::string_view> SocketStream::receive() {
    //there is no lock as the receive is not marked MT safe
    //if putback buffer or is eof, return buffer
    if (!_input_buffer_ready.empty() || _is_eof) return std::exchange(_input_buffer_ready, {});
    //update timeout
    current_recv_tm = _tms._receive;
    update_timer();
    //otherwise it could be resolved asynchronously
    return [this](awaitable<std::string_view>::result r) {
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


void SocketStream::close() {
    std::lock_guard _(_mx);
    //if clear to send - set eof immediately
    if (_clear_to_send) {
        _ctx->send(_h,{});//SEND EOF
        _output_closed = true;
    } else {
        _send_eof = true;   //send asynchronous
    }

}

void  SocketStream::notify_awaiters_ok() {
    //process all awaiters until counter is reached
    while (!_awaiting_results.empty() && _awaiting_results.front().first <= _count_output_bytes) {
        //enqueue true resolution
        _ctx->enqueue(_awaiting_results.front().second(true));
        //pop it
        _awaiting_results.pop();
    }
}
void  SocketStream::notify_awaiters_error() {
    //process all awaiters
    while (!_awaiting_results.empty()) {
        //enqueeu fasle resolution
        _ctx->enqueue(_awaiting_results.front().second(false));
        //pop it
        _awaiting_results.pop();
    }

}


awaitable<bool> SocketStream::send(std::string_view data) {
    //lock now
    std::lock_guard _(_mx);
    //if ouput is closed, nothing can be sent
    if (_send_eof || _output_closed) return false;
    //if clear to send - we can send now
    if (_clear_to_send) {
        //just sync - it is already synced
        if (data.empty()) return true;
        //send all data directly
        auto sz = _ctx->send(_h, data);
        //count sent data
        _count_output_bytes+=sz;
        //if sent everything, return success
        if (sz == data.size()) return true;
        //request for send
        ready_to_send();
        //retrieve remaining data
        data = data.substr(sz);
    }
    //push data to buffer
    _output_buffer.push(data);
    //calculate position of data on global counter
    auto pos = _count_output_bytes + _output_buffer.size();
    //return lambda to asynchronous processing
    return [this,pos](awaitable<bool>::result r) -> prepared_coro{
        //check if detached
        if (r) {
            //so now the coroutine wants to wait until counter reaches the requested value
            std::lock_guard _(_mx);
            //if already reached the counter, return true
            if (_count_output_bytes >= pos) return r(true);
            //if output has been closed, return false
            if (_output_closed) return r(false);
            //otherwise register to awaiter list
            _awaiting_results.push({pos,std::move(r)});
        }
        //in all cases return no coroutine to resume
        return {};
    };

}

void SocketStream::clear_to_send() noexcept {
    //called when we are clear to send
    std::lock_guard _(_mx);
    //if output is already closed, do nothing
    if (_output_closed) return;
    //if buffer is not empty
    if (!_output_buffer.empty()) {
        //retrieve data
        auto data = _output_buffer.front();
        //send data
        auto sz = _ctx->send(_h, data);
        //commit sent data
        _output_buffer.pop(sz);
        //any size sent
        if (sz) {
            //report that sent ok
            notify_awaiters_ok();
            //manifest ready to send
            ready_to_send();
        } else { //nothing sent, connection reset
            //report that sent failed
            notify_awaiters_error();
            //output is closed
            _output_closed = true;
            //we don't manifest ready to send
        }
    } else {//buffer is empty
        //if send_eof is active ...
        if (_send_eof) {
            _ctx->send(_h,{}); //send_eof;
            _output_closed = true;//output closed
        } else {
            //otherwise remember that we are clear to send
            _clear_to_send = true;
            //reset current send timeout
            current_send_tm = current_send_tm.max();
            //update timer
            update_timer();
            //no longer in openning state
            _opening_state = false;
            //sync
            notify_awaiters_ok();
        }
    }

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
            notify_awaiters_error();
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

std::size_t SocketStream::get_buffered_count() const {
    std::lock_guard _(_mx);
    return _output_buffer.size();
}

Stream SocketStream::connect(std::shared_ptr<INetContext> ctx, std::string address_port) {
    auto h = ctx->connect(std::move(address_port));
    return create(std::move(ctx), h);
}

Stream SocketStream::connect(std::shared_ptr<INetContext> ctx, SpecialConnection type, const void *arg) {
    auto h = ctx->connect(type, arg);
    return create(std::move(ctx), h);
}

TCPServer::AWT TCPServer::accept() {
    return [this](PROM r) -> prepared_coro{
        AWT *need = nullptr;
        AWT *prom = r.release();
        if (_r.compare_exchange_strong(need, prom)) {
            _ctx->accept(_h, this);
            return {};
        }
        PROM res(prom);
        return res.drop();
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



async_generator<Stream> SocketStream::create_tcp_server(std::shared_ptr<INetContext> ctx, std::string address_port, std::stop_token stp) {
    return create_tcp_server(std::move(ctx), std::move(address_port), std::move(stp),[](auto &&){return true;});
}

TCPServer::~TCPServer() {
    _ctx->destroy(_h);
}

prepared_coro TCPServer::cancel() {
    static AWT filler;
    auto r = _r.exchange(&filler);
    PROM res(r);
    return res.drop();
}

}
