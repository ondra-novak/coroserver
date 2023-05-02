#include "socket_stream.h"
#include "io_context.h"

#include <sys/socket.h>
namespace coroserver {


SocketStream::SocketStream(SocketSupport context, SocketHandle h, PeerName peer, TimeoutSettings tms)
:AbstractStreamWithMetadata(std::move(tms))
,_ctx(std::move(context))
,_h(h)
,_peer(std::move(peer))
,_reader(start_read())
,_writer(start_write())
{

}

SocketStream::~SocketStream() {
    _ctx.close(_h);
}
cocls::generator<std::string_view> SocketStream::start_read() {
    while (true) {
        std::string_view data;
        while (!_is_eof && data.empty()) {
            _read_buffer.resize(_new_buffer_size);
            int r = ::recv(_h, _read_buffer.data(), _read_buffer.size(), MSG_DONTWAIT|MSG_NOSIGNAL);
            if (r > 0) {
                _cntr.read+=r;
                std::size_t sz = static_cast<std::size_t>(r);
                data = std::string_view(_read_buffer.data(), sz);
                bool was_full = sz == _read_buffer.size();
                if (_last_read_full) {
                    _new_buffer_size = _last_read_full+r;
                }
                _last_read_full = was_full?_read_buffer.size():0;

            } else if (r == 0) {
                _is_eof = true;
            } else {
                int err = errno;
                if (err == EWOULDBLOCK || err == EAGAIN) {
                    _last_read_full = 0;
                    WaitResult w = co_await _ctx.io_wait(_h,AsyncOperation::read,
                            _tms.from_duration(_tms.read_timeout_ms));
                    switch (w) {
                        case WaitResult::closed:
                            _is_eof = true;
                            break;
                        case WaitResult::timeout:
                            _is_timeout = true;
                            co_yield std::string_view();
                            _is_timeout = false;
                        default:
                            break;
                    }
                } else {
                    throw std::system_error(err, std::system_category(), "recv()");
                }
            }
        }
        co_yield data;
    }
}

cocls::future<std::string_view> SocketStream::read() {
    auto buff = read_putback_buffer();
    if (!buff.empty() || _reader.done()) return cocls::future<std::string_view>::set_value(buff);
    return [&]{return _reader();};
}

std::string_view SocketStream::read_nb() {
    auto buff = read_putback_buffer();
    if (!buff.empty() || _is_eof) return buff;
    int r = ::recv(_h, _read_buffer.data(), _read_buffer.size(), MSG_DONTWAIT|MSG_NOSIGNAL);
    if (r >= 0) {
        _is_eof = r == 0;
        _cntr.read+=r;
        buff = std::string_view(_read_buffer.data(), r);
        return buff;
    } else {
        int err = errno;
        if (err == EWOULDBLOCK || err == EAGAIN) {
            return buff;
        } else {
            throw std::system_error(err, std::system_category(), "recv()");
        }
    }
}

bool SocketStream::is_read_timeout() const {
    return _is_timeout;
}

cocls::future<bool> SocketStream::write(std::string_view buffer) {
    if (_writer.done()) return cocls::future<bool>::set_value(false);
    return [&]{return _writer(buffer);};
}

cocls::future<bool> SocketStream::write_eof() {
    if (_is_closed) return cocls::future<bool>::set_value(false);
    ::shutdown(_h, SHUT_WR);
    _is_closed = true;
    return cocls::future<bool>::set_value(true);
}

void SocketStream::shutdown() {
    _is_closed = true;
    _is_eof = true;
    _is_timeout = false;
    _ctx.mark_closing(_h);
}

cocls::generator<bool, std::string_view> SocketStream::start_write() {
    std::string_view buff = co_yield nullptr;
    while (true) {
        while (!_is_closed && !buff.empty()) {
            int r = ::send(_h, buff.data(), buff.size(), MSG_DONTWAIT|MSG_NOSIGNAL);
            if (r >= 0) {
                _cntr.write+=r;
                buff = buff.substr(r);
                _is_closed = r == 0;
            } else {
                int err = errno;
                if (err == EWOULDBLOCK || err == EAGAIN) {
                    WaitResult w = co_await _ctx.io_wait(_h, AsyncOperation::write,
                            _tms.from_duration(_tms.write_timeout_ms));
                    switch(w) {
                        case WaitResult::timeout:
                        case WaitResult::closed:
                            _is_closed = true;
                            break;
                        default:
                            break;
                    }
                } else if (err == EPIPE) {
                    _is_closed = true;
                } else {
                    throw std::system_error(err, std::system_category(), "send()");
                }
            }
        }
        buff = co_yield !_is_closed;
    }
}

SocketStream::Counters SocketStream::get_counters() const noexcept {
    return _cntr;
}

PeerName SocketStream::get_peer_name() const {
    return _peer;
}


}
