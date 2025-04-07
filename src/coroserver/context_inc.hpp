#pragma once

#include <thread>
#include <vector>

namespace coroserver {


template<typename Fn>
auto AbstractHandleData::visit(Fn &&fn) {
    switch (_type) {
        case HandleType::socket: return fn(*static_cast<StreamHandleData *>(this));
        case HandleType::server: return fn(*static_cast<ServerHandleData *>(this));
        case HandleType::timer: return fn(*static_cast<TimerHandleData *>(this));
        case HandleType::two_pipes: return fn(*static_cast<TwoPipesStreamData *>(this));
        case HandleType::signalfd:return fn(*static_cast<SigHandleData *>(this));
        default:throw std::logic_error("unknown handle data");
    }
}
template<typename Fn>
auto AbstractHandleData::visit(Fn &&fn) const {
    switch (_type) {
        case HandleType::socket: return fn(*static_cast<const StreamHandleData *>(this));
        case HandleType::server: return fn(*static_cast<const ServerHandleData *>(this));
        case HandleType::timer: return fn(*static_cast<const TimerHandleData *>(this));
        case HandleType::two_pipes: return fn(*static_cast<const TwoPipesStreamData *>(this));
        case HandleType::signalfd:return fn(*static_cast<const SigHandleData *>(this));
        default:throw std::logic_error("unknown handle data");
    }
}

void HandleDataDeleter::operator()(AbstractHandleData *p) {
    p->visit([](auto &q){
        delete (&q);
    });
}



Context::Context(std::shared_ptr<ContextImpl> impl):_impl(std::move(impl)) {

}

Context::Handle Context::create_server(std::string host, std::string def_port) {
    return _impl->create_server(std::move(host), std::move(def_port));
}

Context::Handle Context::connect(std::string host, std::string def_port) {
    return _impl->connect(std::move(host), std::move(def_port));
}

Context::Handle Context::create_process(std::string_view path, std::span<const std::string_view> argv,  const Environment & envp) {
    return _impl->connect_process(path, argv, envp);
}

Context::Handle Context::connect_stdinout() {
    return _impl->connect_stdinout();
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

bool Context::terminate_process(Handle h) {
    return _impl->terminate_process(h);
}

coro::awaitable<int> Context::get_process_exit_status(Handle h, std::chrono::system_clock::time_point tp) {
    return _impl->get_process_exit_status(h, tp);
}

coro::awaitable<BreakType> Context::wait_on_break() {
    return _impl->wait_on_break();
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

