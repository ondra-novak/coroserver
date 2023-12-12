#include "socket_stream.h"
#include "io_context.h"

#include <sys/socket.h>
namespace coroserver {

SocketStream::SocketStream(AsyncSocket socket, PeerName peer, TimeoutSettings tms)
:AbstractStreamWithMetadata(std::move(tms))
,_socket(std::move(socket))
,_peer(std::move(peer)) {
    coro::target_member_fn_activation<&SocketStream::read_completion>(_wait_read_target, this);
    coro::target_member_fn_activation<&SocketStream::write_completion>(_wait_write_target, this);
}

bool SocketStream::read_begin(std::string_view &buff) {
    buff = this->read_putback_buffer();
    if (!buff.empty() || _is_eof) return true;
    _read_buffer.resize(_new_buffer_size);
    int r = ::recv(_socket, _read_buffer.data(), _read_buffer.size(), MSG_DONTWAIT| MSG_NOSIGNAL);
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

coro::future<std::string_view> SocketStream::read() {
    std::string_view buff;
    if (read_begin(buff)) return buff;
    return [&](auto p) {
        _read_promise = std::move(p);
        _wait_read_result << [&]{return _socket.io_wait(AsyncOperation::read,
                TimeoutSettings::from_duration(_tms.read_timeout_ms));};
        _wait_read_result.register_target(_wait_read_target);
    };
}

std::string_view SocketStream::read_nb() {
    std::string_view buff;
    read_begin(buff);
    return buff;
}

void SocketStream::read_completion(coro::future<bool> *f) noexcept {
    try {
        if (f->has_value()) {
            bool st = *f;
            if (st) {
                _read_promise(read_nb());
            } else {
                _read_promise();
            }
        } else {
            _is_eof = true;
            _read_promise();
        }
    } catch (...) {
        _read_promise.reject();
    }
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
    int r;
    do {
        r = ::send(_socket, _write_buffer.data(), _write_buffer.size(), MSG_DONTWAIT|MSG_NOSIGNAL);
        if (r > 0) {
            auto sub = _write_buffer.substr(r);
            _cntr.write+=r;
            if (sub.empty()) {
                _write_promise(true);
                return ;
            }
            _write_buffer = sub;
        }
    } while (r > 0);
    if (r < 0) {
        int e = errno;
        if (e == EWOULDBLOCK|| e == EAGAIN) {
            _wait_write_result << [&]{return _socket.io_wait(AsyncOperation::write,
                    TimeoutSettings::from_duration(_tms.write_timeout_ms));};
            _wait_write_result.register_target(_wait_write_target);
        } else if (e == EPIPE) {
            _is_closed = true;
            _write_promise(false);
        } else {
            throw std::system_error(e, std::system_category(), "send failed");
        }
    } else {
        throw std::system_error(EPIPE, std::system_category(), "send returned 0");
    }
}

void SocketStream::write_completion(coro::future<bool> *f) noexcept {
    try {
        if (f->has_value()) {
            bool st = *f;
            if (st) {
                write_begin();
            } else {
                _write_promise(false);
            }
        } else {
            _is_closed = true;
            _write_promise(false);
        }
    } catch (...) {
        _write_promise.reject();
    }
}

coro::future<bool> SocketStream::write_eof() {
    if (_is_closed) return false;
    ::shutdown(_socket, SHUT_WR);
    _is_closed = true;
    return true;
}

void SocketStream::shutdown() {
    return _socket.shutdown();
}


SocketStream::Counters SocketStream::get_counters() const noexcept {
    return _cntr;
}

PeerName SocketStream::get_peer_name() const {
    return _peer;
}


}
