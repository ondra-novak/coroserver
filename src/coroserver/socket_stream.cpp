
#include "socket_stream.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <linux/sockios.h>
namespace coroserver {

SocketStream::SocketStream(AsyncResource *socket,
        AsyncEngine engine,
        PeerName peer,
        TimeoutSettings tms)
:AbstractStreamWithMetadata(std::move(tms))
,_socket(std::move(socket))
,_engine(std::move(engine))
,_peer(std::move(peer)) {
}

#if 0
bool SocketStream::read_begin(std::string_view &buff) {
    buff = this->read_putback_buffer();
    if (!buff.empty() || _is_eof) return true;
    _read_buffer.resize(_new_buffer_size);
    _wait_read_result << [&]{
        return _engine.recv(_socket,_read_buffer.data(), _read_buffer.size());
    };
    if (!_wait_read_result.is_pending())
        int r = f.get();
        if (r > 0 && static_cast<std::size_t>(r) == _read_buffer.size()) {
            _new_buffer_size = _new_buffer_size*3/2;
        }
    }
    if (r>0) {
        buff = std::string_view(_read_buffer.data(), r);
        if (buff.size() == _read_buffer.size()) {
            _new_buffer_size = _new_buffer_size*3/2;
        }
        _cntr.read+=r;
        return true;
    } else if (r<0) {
        int e = errno;
        if (e == EWOULDBLOCK || e == EAGAIN) {
            return false;
        } else {
            throw std::system_error(e, std::system_category(), "recv failed");
        }
    } else {
        _is_eof = true;
        return true;
    }

}

void SocketStream::enable_nagle() {
   if (_nagle_state.test_and_set(std::memory_order_relaxed)) return;
   int flag = 0;

   setsockopt(_socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int));
}

void SocketStream::disable_nagle() {
   _nagle_state.clear(std::memory_order_relaxed);
   int flag = 1;

   setsockopt(_socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int));
}

#endif

coro::future<std::string_view> SocketStream::read() {
    return [&](auto p) {
        std::string_view buff = this->read_putback_buffer();
        if (_is_eof || !buff.empty()) {
            p(buff);
            return;
        }
        _read_promise = std::move(p);
        _read_buffer.resize(_new_buffer_size);
        _wait_read_result << [&]{
            return _engine.recv(_socket, _read_buffer.data(), _read_buffer.size(),
                    _tms.get_read_timeout());
        };
        auto finish = [this]{
            if (_wait_read_result.has_value()) {
                int r = _wait_read_result.get();
                if (r < 0) {
                    _read_promise();
                } else if (r == 0) {
                    _is_eof = true;
                    _read_promise();
                } else {
                    std::size_t sz = static_cast<std::size_t>(r);
                    _cntr.read+=sz;
                    _read_promise(_read_buffer.data(), sz);
                }
            } else {
                _is_eof = true;
                _read_promise();
            }
        };
        if (!_wait_read_result.set_callback(finish)) {
            int r = _wait_read_result.get();
            if (r > 0 && static_cast<std::size_t>(r) == _read_buffer.size()) {
                _new_buffer_size = _new_buffer_size*3/2;
            }
            finish();
        }
    };
}

std::string_view SocketStream::read_nb() {
    std::string_view buff = this->read_putback_buffer();
    if (_is_eof || !buff.empty()) {
        return buff;
    }
    _read_buffer.resize(_new_buffer_size);
    int r = _engine.recv_nb(_socket, _read_buffer.data(), _read_buffer.size());
    return {_read_buffer.data(), static_cast<std::size_t>(r)};
}


bool SocketStream::is_read_timeout() const {
    return !_is_eof;
}

coro::future<bool> SocketStream::write(std::string_view buffer) {
    return [&](auto p) {
        _write_promise = std::move(p);
        _write_buffer = buffer;
        write_begin();
    };
}
void SocketStream::write_begin() {
    if (_is_closed) {
        _write_promise(false);
        return;
    }
    if (_write_buffer.empty()) {
        _write_promise(true);
        return;
    }
    _wait_write_result << [&]{
        return _engine.send(_socket, _write_buffer.data(), _write_buffer.size(),
                _tms.get_write_timeout());
    };

    _wait_write_result >> [this]{
        if (_wait_write_result.has_value()) {
            int r = _wait_write_result.get();
            if (r > 0) {
                std::size_t sz = static_cast<std::size_t>(r);
                _write_buffer = _write_buffer.substr(sz);
                _cntr.write+=sz;
                if (_write_buffer.empty()) _write_promise(true);
                this->write_begin();
            } else {
                _write_promise(false);
            }
        } else {
            _write_promise(false);
        }
    };

}

coro::future<bool> SocketStream::write_eof() {
    if (_is_closed) return false;
    _engine.send_eof(_socket);
    _is_closed = true;
    return true;
}

void SocketStream::shutdown() {
    return _engine.shutdown(_socket);
}


SocketStream::Counters SocketStream::get_counters() const noexcept {
    return _cntr;
}

PeerName SocketStream::get_peer_name() const {
    return _peer;
}


static coro::coroutine shutdown_slow(AsyncResource *h, AsyncEngine engine) {
    auto max_wait = std::chrono::system_clock::now()+std::chrono::seconds(30);
    //if there still some data
    try {
        while (engine.get_siocoutq(h) > 0 && max_wait > std::chrono::system_clock::now()) {
            //wait for
            char buff[1024];
            auto p = engine.recv(h, buff, sizeof(buff), TimeoutSettings::from_duration(std::chrono::milliseconds(200)));
            //co_await and check status - no value mean, we can no longer wait
            if (co_await !p )
                break;
            if (p.get() == 0) { //if connection has been closed, than exit too
                break;
            }

        }
    } catch (...) {
        //empty
    }
    engine.close_handle(h);
}



SocketStream::~SocketStream() {
    _engine.send_eof(_socket);
    if (_engine.get_siocoutq(_socket)> 0) {
        shutdown_slow(_socket, std::move(_engine));
    } else {
        _engine.close_handle(_socket);
    }
}


Stream SocketStream::create(AsyncResource *socket,
        AsyncEngine engine,
        PeerName peer,
        TimeoutSettings tms) {
    return Stream(std::make_shared<SocketStream>(
            std::move(socket),
            std::move(engine),
            std::move(peer),
            std::move(tms)));
}

}
