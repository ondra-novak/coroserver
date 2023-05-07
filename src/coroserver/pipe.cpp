#include "pipe.h"

#include <fcntl.h>
namespace coroserver {

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}


PipeStream::PipeStream(SocketSupport context, int fdread, int fdwrite,TimeoutSettings tms)
:AbstractStreamWithMetadata(std::move(tms))
,_ctx(std::move(context))
,_fdread(fdread)
,_fdwrite(fdwrite)
,_reader(start_read())
,_writer(start_write())
{
    if (_fdread>=0) set_nonblocking(_fdread);
    if (_fdwrite>=0) set_nonblocking(_fdwrite);
}


PipeStream::~PipeStream() {
    if (_fdread>=0) _ctx.close(_fdread);
    if (_fdwrite>=0 && _fdwrite != _fdread) _ctx.close(_fdwrite);
}
cocls::generator<std::string_view> PipeStream::start_read() {
    while (true) {
        std::string_view data;
        while (!_is_eof && data.empty()) {
            _read_buffer.resize(_new_buffer_size);
            int r = ::read(_fdread, _read_buffer.data(), _read_buffer.size());
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
                    WaitResult w = co_await _ctx.io_wait(_fdread,AsyncOperation::read,
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

cocls::future<std::string_view> PipeStream::read() {
    auto buff = read_putback_buffer();
    if (!buff.empty() || _reader.done()) return cocls::future<std::string_view>::set_value(buff);
    return [&]{return _reader();};
}

std::string_view PipeStream::read_nb() {
    auto buff = read_putback_buffer();
    if (!buff.empty() || _is_eof) return buff;
    int r = ::read(_fdread, _read_buffer.data(), _read_buffer.size());
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

bool PipeStream::is_read_timeout() const {
    return _is_timeout;
}

cocls::future<bool> PipeStream::write(std::string_view buffer) {
    if (_writer.done()) return cocls::future<bool>::set_value(false);
    return [&]{return _writer(buffer);};
}

cocls::future<bool> PipeStream::write_eof() {
    if (_is_closed) return cocls::future<bool>::set_value(false);
    if (_fdwrite>=0) {
        _ctx.close(_fdwrite);
        _fdwrite =  -1;
    }
    _is_closed = true;
    return cocls::future<bool>::set_value(true);
}

cocls::suspend_point<void> PipeStream::shutdown() {
    _is_closed = true;
    _is_eof = true;
    _is_timeout = false;
    cocls::suspend_point<void> out;
    if (_fdread >= 0) out << _ctx.mark_closing(_fdread);
    if (_fdwrite >= 0) out << _ctx.mark_closing(_fdwrite);
    return out;
}

Stream PipeStream::create(SocketSupport context, TimeoutSettings tms) {
    int p[2];
    int e = pipe2(p, O_CLOEXEC| O_NONBLOCK);
    if (e) throw std::system_error(errno, std::system_category(), "PipeStream::create/pipe2");
    return Stream(std::make_shared<PipeStream>(context, p[0], p[1], tms));
}

static int dup_desc(int src, bool share, int dup_to) {
    int e = 0;
    if (dup_to >= 0) {
        if (share) {
          e = dup2(src, dup_to);
        } else {
          e = dup3(src, dup_to, O_CLOEXEC);
        }
    } else {
        if (share) {
            dup_to = dup(src);
        } else {
            dup_to = fcntl(src, F_DUPFD_CLOEXEC, 0);
        }
        if (dup_to < 0) e = dup_to;
    }
    if (e) throw std::system_error(errno, std::system_category(), "dup()");
    return dup_to;
}

static PipeStream &getInstance(Stream stream) {
    return  dynamic_cast<PipeStream &>(*stream.getStreamDevice());
}

int PipeStream::dupRead(Stream stream, bool share, bool close, int dup_to) {
    PipeStream &s = getInstance(stream);
    dup_to = dup_desc(s._fdread, share, dup_to);
    if (close) {
        s._ctx.close(s._fdread);
        s._fdread = -1;
    }
    return dup_to;
}

int PipeStream::dupWrite(Stream stream, bool share, bool close, int dup_to) {
    PipeStream &s = getInstance(stream);
    dup_to = dup_desc(s._fdwrite, share, dup_to);
    if (close) {
        s._ctx.close(s._fdwrite);
        s._fdwrite= -1;
    }
    return dup_to;
}

Stream PipeStream::create(SocketSupport context, int fdread, int fdwrite, TimeoutSettings tms) {
    return Stream(std::make_shared<PipeStream>(context, fdread, fdwrite, tms));
}

Stream PipeStream::create(SocketSupport context, int fd, TimeoutSettings tms) {
    return Stream(std::make_shared<PipeStream>(context, fd, fd, tms));
}

Stream PipeStream::stdio(SocketSupport context, TimeoutSettings tms, bool duplicate) {
    int rd = duplicate?dup_desc(0, false, -1):0;
    int wr = duplicate?dup_desc(1, false, -1):1;
    return Stream(std::make_shared<PipeStream>(context, rd, wr, tms));
}

cocls::generator<bool, std::string_view> PipeStream::start_write() {
    std::string_view buff = co_yield nullptr;
    while (true) {
        while (!_is_closed && !buff.empty()) {
            int r = ::write(_fdwrite, buff.data(), buff.size());
            if (r >= 0) {
                _cntr.write+=r;
                buff = buff.substr(r);
                _is_closed = r == 0;
            } else {
                int err = errno;
                if (err == EWOULDBLOCK || err == EAGAIN) {
                    WaitResult w = co_await _ctx.io_wait(_fdwrite, AsyncOperation::write,
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

PipeStream::Counters PipeStream::get_counters() const noexcept {
    return _cntr;
}

PeerName PipeStream::get_peer_name() const {
    return {};
}

}
