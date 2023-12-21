#include "context.h"

#include "local_stream.h"
#include <unistd.h>
#include <poll.h>

namespace coroserver {

LocalStream::LocalStream(AsyncSocket read_fd,  AsyncSocket write_fd, PeerName peer, TimeoutSettings tms)
:AbstractStreamWithMetadata(std::move(tms))
,_read_fd(std::move(read_fd))
,_write_fd(std::move(write_fd))
,_peer(std::move(peer)) {
    coro::target_member_fn_activation<&LocalStream::read_completion>(_wait_read_target, this);
    coro::target_member_fn_activation<&LocalStream::write_completion>(_wait_write_target, this);
}

bool LocalStream::read_begin(std::string_view &buff) {
    buff = this->read_putback_buffer();
    if (!buff.empty() || _is_eof) return true;
    _read_buffer.resize(_new_buffer_size);
    if (!read_available()) return false;
    int r = ::read(_read_fd, _read_buffer.data(), _read_buffer.size());
    if (r>0) {
        buff = std::string_view(_read_buffer.data(), r);
        if (buff.size() == _read_buffer.size()) {
            _new_buffer_size = _new_buffer_size*3/2;
        }
        _cntr.read+=r;
        return true;
    } else if (r<0) {
        int e = errno;
         throw std::system_error(e, std::system_category(), "recv failed");
    } else {
        _is_eof = true;
        return true;
    }

}


coro::future<std::string_view> LocalStream::read() {
    std::string_view buff;
    if (read_begin(buff)) return buff;
    return [&](auto p) {
        _read_promise = std::move(p);
        _wait_read_result << [&]{return _read_fd.io_wait(AsyncOperation::read,_tms.get_read_timeout());};
        _wait_read_result.register_target(_wait_read_target);
    };
}

std::string_view LocalStream::read_nb() {
    std::string_view buff;
    read_begin(buff);
    return buff;
}

void LocalStream::read_completion(coro::future<bool> *f) noexcept {
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

bool LocalStream::is_read_timeout() const {
    return !_is_eof;
}

coro::future<bool> LocalStream::write(std::string_view buffer) {
    return [&](auto p) {
        _write_promise = std::move(p);
        _write_buffer = buffer;
        write_begin();
    };
}
void LocalStream::write_begin() {
    if (_is_closed) {
        _write_promise(false);
        return;
    }
    int r;
    do {
        if (!write_available()) {
            _wait_write_result << [&]{return _write_fd.io_wait(AsyncOperation::write, _tms.get_write_timeout());};
            _wait_write_result.register_target(_wait_write_target);
            return;

        }
        r = ::write(_write_fd, _write_buffer.data(), _write_buffer.size());
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
        if (e == EPIPE) {
            _is_closed = true;
            _write_promise(false);
        } else {
            throw std::system_error(e, std::system_category(), "send failed");
        }
    } else {
        throw std::system_error(EPIPE, std::system_category(), "send returned 0");
    }
}

void LocalStream::write_completion(coro::future<bool> *f) noexcept {
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

coro::future<bool> LocalStream::write_eof() {
    if (_is_closed) return false;
    _write_fd = {};
    _is_closed = true;
    return true;
}

void LocalStream::shutdown() {
    if (_write_fd) _write_fd.shutdown();
    if (_read_fd) _read_fd.shutdown();
}


LocalStream::Counters LocalStream::get_counters() const noexcept {
    return _cntr;
}

PeerName LocalStream::get_peer_name() const {
    return _peer;
}

bool LocalStream::read_available() const {
    pollfd pfd = {_read_fd, POLLIN, 0};
    return poll(&pfd,1,0) != 0;

}

bool LocalStream::write_available() const {
    pollfd pfd = {_write_fd, POLLOUT, 0};
    return poll(&pfd,1,0) != 0;
}

}
