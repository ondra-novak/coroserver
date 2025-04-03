#pragma once

#include "context.hpp"
#include "stream.hpp"

namespace coroserver {



class TCPServer {
public:

    TCPServer(Context ctx, Context::Handle h);

    ~TCPServer();

    TCPServer(TCPServer &&other);

    static TCPServer create(Context ctx, std::string host, std::string def_port);

    std::string get_host() const;

    coro::awaitable<Stream> accept();

    coro::awaitable<Stream> accept(std::chrono::system_clock::time_point tp);

    void shutdown();

protected:
    Context _ctx;
    Context::Handle _h = 0;

    coro::awaiting_callback<coro::awaitable<Context::Handle> ,
        TCPServer *, coro::awaitable<Stream>::result> _callback;

    auto process_accept(coro::awaitable<Stream>::result r);
};


}
