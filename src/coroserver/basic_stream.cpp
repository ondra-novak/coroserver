#include "basic_stream.hpp"

namespace coroserver {

BasicStream::BasicStream(Context ctx, Context::Handle h)
:_ctx(std::move(ctx)),_h(std::move(h))
{

}

BasicStream::~BasicStream() {
    _ctx.close(_h);
}

Context BasicStream::get_context() const {
    return _ctx;
}

coro::awaitable<std::string_view> BasicStream::read() {
    if (!_putback_buffer.empty()) return std::exchange(_putback_buffer,{});
    if (_next_buffer_size > _buffer.size()) {
        _buffer.clear();
        _buffer.resize(_next_buffer_size);
    }
    auto awt = _ctx.receive(_h, _buffer.data(), _buffer.size(), _tm._receive.get_time_point());
    if (awt.await_ready()) {
       auto sz = awt.await_resume();
       return std::string_view(_buffer.data(), sz);
    } else {
        _read_awt_cb.prepare_await(awt);
        return [this, _ = _read_awt_cb.get_auto_cancel()](coro::awaitable<std::string_view>::result r){
            return _read_awt_cb.await_on_prepared([this, r = std::move(r)](coro::awaitable<std::size_t> &awt) mutable {
                try {
                    if (!awt.has_value()) {
                        return r.set_empty();
                    }
                    std::size_t sz = awt.await_resume();
                    _cntrs.received+=sz;
                    if (sz == _buffer.size()) _next_buffer_size = _next_buffer_size * 3 / 2;
                    return r.set_value(std::string_view(_buffer.data(), sz));
                } catch(...) {
                    return r.set_exception(std::current_exception());
                }
            });
        };
    }
}

IOTimeout BasicStream::get_timeouts() const {
    return _tm;
}

StreamState BasicStream::get_state() const {
    return _ctx.get_state(_h);
}

void BasicStream::put_back(std::string_view s) {
    _putback_buffer = s;
}

coroserver::IStream::Counters BasicStream::get_counters() const {
    return _cntrs;
}

coro::awaitable<bool> BasicStream::write(std::string_view data) {
    _cntrs.sent += data.size();
    return _ctx.send(_h, data.data(), data.size(), _tm._send.get_time_point());
}

coro::awaitable<bool> BasicStream::close() {
    return _ctx.send_eof(_h);
}

void BasicStream::set_timeouts(IOTimeout tm) {
    _tm = tm;
}

void BasicStream::shutdown() {
    _ctx.shutdown(_h);
}

}
