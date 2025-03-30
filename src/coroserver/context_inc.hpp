#pragma once

#include <thread>
#include <vector>

namespace coroserver {

Context::Context(std::shared_ptr<ContextImpl> impl):_impl(std::move(impl)) {

}

Context::Handle Context::create_server(std::string host, std::string def_port) {
    return _impl->create_server(std::move(host), std::move(def_port));
}

Context::Handle Context::connect(std::string host, std::string def_port) {
    return _impl->connect(std::move(host), std::move(def_port));
}

Context::Handle Context::connect(SpecialDevice dev) {
    return _impl->connect(dev);
}

Context::Handle Context::create_timer() {
    return _impl->create_timer();
}

void Context::close(Handle h) {
    return _impl->close(h);
}

std::string Context::get_host(Handle h) const {
    return _impl->get_host(h);
}

coro::awaitable<bool> Context::sleep(Handle timer,std::chrono::system_clock::time_point tp) {
    return _impl->sleep(timer, tp);
}

coro::awaitable<Context::Handle> Context::accept(Handle server,
                            std::chrono::system_clock::time_point timeout) {
    return _impl->accept(server, timeout);
}

coro::awaitable<size_t> Context::receive(Handle stream,
        char *buffer, std::size_t sz,
        std::chrono::system_clock::time_point timeout) {
    return _impl->receive(stream, buffer, sz, timeout);
}

coro::awaitable<bool> Context::send(Handle stream,
        const char *buffer, std::size_t sz,
        std::chrono::system_clock::time_point timeout) {
    return _impl->send(stream, buffer, sz, timeout);
}

coro::awaitable<bool> Context::send_eof(Handle stream) {
    return _impl->send_eof(stream);
}

StreamState Context::get_state(Handle h) const {
    return _impl->get_state(h);
}

void Context::shutdown(Handle h) {
    return _impl->shutdown(h);
}

class ContextThreaded: public ContextImpl {
public:


    ContextThreaded(unsigned int threads) {
        _thrs.reserve(threads);
        for (unsigned int i = 0; i < threads; ++i) {
            _thrs.emplace_back([this]{
                this->thread_entry_point();
            });
        }
    }

    ~ContextThreaded() {
        this->signal_stop();
    }


protected:
    std::vector<std::jthread> _thrs;

};

Context Context::create(unsigned int iothreads) {
    return Context(std::make_shared<ContextThreaded>(iothreads));
}

Context::~Context() {}

}
