#include "basic_stream.hpp"
#include "server.hpp"

namespace coroserver {

TCPServer::TCPServer(Context ctx, Context::Handle h)
        :_ctx(std::move(ctx)), _h(std::move(h)) {}

TCPServer::~TCPServer() {
    if (_h) _ctx.close(_h);
}

TCPServer::TCPServer(TCPServer &&other):_ctx(std::move(other._ctx)),_h(other._h) {
    other._h = Context::null_handle;
}

TCPServer TCPServer::create(Context ctx, std::string host, std::string def_port) {
    auto h = ctx.create_server(host, def_port);
    return {std::move(ctx), h};
}

std::string TCPServer::get_host() const {
    return _ctx.get_host(_h);
}

auto TCPServer::process_accept(coro::awaitable<Stream>::result r) {
    return [this, r = std::move(r)](coro::awaitable<Context::Handle> &awt) mutable {
        try {
            Context::Handle h = awt.await_resume();
            if (h) {
                return r.set_value(std::make_shared<BasicStream>(_ctx, h));
            } else {
                return r.set_empty();
            }
        } catch (...) {
            return r.set_exception(std::current_exception());
        }
    };
}

coro::awaitable<Stream> TCPServer::accept() {
    return [this](coro::awaitable<Stream>::result r){
        _callback.await(_ctx.accept(_h, std::chrono::system_clock::time_point::max()),
            process_accept(std::move(r)));
    };
}

coro::awaitable<Stream> TCPServer::accept(std::chrono::system_clock::time_point tp) {
    return [this,tp](coro::awaitable<Stream>::result r){
        _callback.await(_ctx.accept(_h, tp),
            process_accept(std::move(r)));
    };
}

void TCPServer::shutdown() {
    _ctx.shutdown(_h);
}

}
