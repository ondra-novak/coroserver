#include "async_engine_epoll.h"

#include "async_engine.h"

namespace coroserver {


AsyncEngine::AsyncEngine():_ptr(std::make_shared<AsyncEngineImpl>()) {
}

AsyncEngine::~AsyncEngine() {
}

AsyncEngine::RetVal AsyncEngine::recv(Handle h, void *buffer, std::size_t size,
        Timepoint timeout) {
    return _ptr->recv(h, buffer, size, timeout);
}

int AsyncEngine::recv_nb(Handle h, void *buffer, std::size_t size) {
    return _ptr->recv_nb(h, buffer, size);
}

AsyncEngine::RetVal AsyncEngine::send(Handle h, const void *buffer,
        std::size_t size, Timepoint timeout) {
    return _ptr->send(h, buffer, size, timeout);
}

AsyncEngine::RetVal AsyncEngine::accept(Handle h, Handle &retHandle,
        PeerName &retPeerName, Timepoint timeout) {
    return _ptr->accept(h, retHandle, retPeerName, timeout);
}

void AsyncEngine::send_eof(Handle h) {
    return _ptr->send_eof(h);
}

void AsyncEngine::close_handle(Handle h) {
    return _ptr->close_handle(h);
}

AsyncEngine::Blocker AsyncEngine::block_async(Handle h) {
    _ptr->block(h, true);
    return {this, {h}};
}

void AsyncEngine::shutdown(Handle h) {
    _ptr->block(h, true);
}

AsyncEngine::Task AsyncEngine::wait_for_next_event(Timepoint timeout) {
    return _ptr->wait_for_next_event(timeout);
}

AsyncEngine::UniqueHandle AsyncEngine::connect_special(SpecialDevice dev) {
    return UniqueHandle(_ptr->connect_special(dev), _ptr);
}

coro::future<AsyncEngine::UniqueHandle> AsyncEngine::connect(const PeerName &target, Timepoint timeout, std::stop_token cancel) {
    Handle h = _ptr->connect(target);

    std::stop_callback _(cancel, [&]{
        _ptr->block(h,true);
    });

    try {
        int r = co_await _ptr->wait_connect(h, timeout);
        if (r == -1) throw std::system_error(ETIMEDOUT, std::system_category(), "Connect timeout");
        if (r == 0) throw coro::await_canceled_exception();
        co_return UniqueHandle(h, _ptr);
    } catch (...) {
        _ptr->close_handle(h);
        throw;
    }
}

coro::future<AsyncEngine::UniqueHandle> AsyncEngine::connect_named_pipe(OperationMode mode, const std::string &name, Timepoint timeout, std::stop_token cancel) {
    Handle h = _ptr->create_named_pipe(mode, name);

    std::stop_callback _(cancel, [&]{
        _ptr->block(h,true);
    });

    try {
        int r = co_await _ptr->wait_connect(h, timeout);
        if (r == -1) throw std::system_error(ETIMEDOUT, std::system_category(), "Connect timeout");
        if (r == 0) throw coro::await_canceled_exception();
        co_return UniqueHandle(h, _ptr);
    } catch (...) {
        _ptr->close_handle(h);
        throw;
    }
}

AsyncEngine::UniqueHandle AsyncEngine::open_file(OperationMode mode, const std::string &name, bool append) {
    return UniqueHandle(_ptr->open_file(mode, name, append), _ptr);
}

AsyncEngine::UniqueHandle AsyncEngine::listen(const PeerName &ifc) {
    return {_ptr->listen(ifc), _ptr};
}

AsyncEngine::Task AsyncEngine::wait_for_next_event() {
    return _ptr->wait_for_next_event();
}

void AsyncEngine::cancel_wait_for_next_event() {
    _ptr->cancel_wait_for_next_event();
}


void AsyncEngine::BlockingDeleter::operator ()(AsyncEngine *ptr) {
    ptr->_ptr->block(h, false);
}

int AsyncEngine::get_siocoutq(Handle h) {
    return _ptr->get_siocoutq(h);
}

PeerName AsyncEngine::get_name(Handle h) {
    return _ptr->get_name(h);
}


void AsyncEngine::UniqueHandleDeleter::operator()(AsyncResource *h) {
    _ptr->close_handle(h);
}

AsyncEngine::AsyncEngine(std::shared_ptr<AsyncEngineImpl> ptr):_ptr(ptr) {}

AsyncEngine AsyncEngine::get_engine(const UniqueHandle &h) {
    return AsyncEngine(h.get_deleter()._ptr);
}
void AsyncEngine::block_all(bool block) {
    _ptr->block_all(block);
}
AsyncEngine::UniqueHandle AsyncEngine::open_intr_signal_listener() {
    return {_ptr->install_intr_signal(), _ptr};
}

}
