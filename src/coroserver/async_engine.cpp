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

coro::future<AsyncEngine::Handle> AsyncEngine::connect(const PeerName &target, Timepoint timeout) {
    Handle h = _ptr->connect(target);
    try {
        int r = co_await _ptr->wait_connect(h, timeout);
        if (r == -1) throw std::system_error(ETIMEDOUT, std::system_category(), "Connect timeout");
        co_return h;
    } catch (...) {
        _ptr->close_handle(h);
        throw;
    }
}

AsyncEngine::Handle AsyncEngine::listen(const PeerName &ifc) {
    return _ptr->listen(ifc);
}

AsyncEngine::Task AsyncEngine::wait_for_next_event() {
    return _ptr->wait_for_next_event();
}

void AsyncEngine::BlockingDeleter::operator ()(AsyncEngine *ptr) {
    ptr->_ptr->block(h, false);
}

int AsyncEngine::get_siocoutq(Handle h) {
}

}
