#pragma once

#include "istream.hpp"
#include "context.hpp"

namespace coroserver {

class BasicStream: public IStream {
public:

    BasicStream(Context ctx, Context::Handle h);
    virtual ~BasicStream();
    BasicStream(const BasicStream &) = delete;
    BasicStream &operator=(const BasicStream &) = delete;

    virtual Context get_context() const override;
    virtual coro::awaitable<std::string_view> read() override;
    virtual IOTimeout get_timeouts() const override;
    virtual StreamState get_state() const override;
    virtual void put_back(std::string_view s) override;
    virtual IStream::Counters get_counters() const override;
    virtual coro::awaitable<bool> write(std::string_view data) override;
    virtual coro::awaitable<bool> close() override;
    virtual void set_timeouts(IOTimeout tm) override;
    virtual void shutdown() override;

    Context::Handle get_handle() const {return _h;}

protected:
    Context _ctx;
    Context::Handle _h;
    IOTimeout _tm;
    Counters _cntrs;

    coro::awaiting_callback<coro::awaitable<std::size_t>,
            BasicStream *, coro::awaitable<std::string_view>::result> _read_awt_cb;

    std::size_t _next_buffer_size = 1500;
    std::vector<char> _buffer;
    std::string_view _putback_buffer;


};

}
