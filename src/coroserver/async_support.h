#pragma once

#ifndef SRC_COROSERVER_SOCKET_SUPPORT_
#define SRC_COROSERVER_SOCKET_SUPPORT_

#include "defs.h"
#include <coro.h>
#include <memory>
#include <any>


namespace coroserver {


void close_socket(const SocketHandle &handle);


class IAsyncSupport {
public:


    using TimerCancel = coro::any<>;

    virtual ~IAsyncSupport() = default;
    ///wait for stream input
    /**
     * @param handle handle of stream
     * @param timeout timeout
     * @retval true data arrived
     * @retval false timeout
     * @retval /canceled/ connection shutdown
     */
    virtual coro::future<bool> input(SocketHandle handle, std::chrono::system_clock::time_point timeout) = 0;
    ///wait for stream output
    /**
     * @param handle handle of stream
     * @param timeout timeout
     * @retval true clear to send data
     * @retval false timeout
     * @retval /canceled/ connection shutdown
     */
    virtual coro::future<bool> output(SocketHandle handle, std::chrono::system_clock::time_point timeout) = 0;
    ///Shutdown the connection
    virtual void shutdown(SocketHandle handle) = 0;
    ///Close connection - release handle
    virtual void close(SocketHandle handle) = 0;
    ///Create timer
    /**
     * @param tp time when timer is triggered
     * @param ident optinal identification
     * @retval true time reached
     * @retval false timer canceled
     * @retval /canceled/ timer canceled because context is canceled
     */
    virtual coro::future<bool> timer(std::chrono::system_clock::time_point tp, const void *ident) = 0;
    ///Cancel time
    /**
     * @param ident identity of timer
     * @return object, which prevents timer creation while it is held.
     */
    virtual TimerCancel cancel_timer(const void *ident) = 0;
};



///Contains asynchronous socket
/**
 * @tparam SocketHandle type of socket handle (platform depend)
 */

class AsyncSocket {
public:

    AsyncSocket() = default;
    ///construct async socket
    /**
     * @param h handle socket
     * @param async_support pointer to async support (aka context)
     */
    AsyncSocket(SocketHandle h, std::shared_ptr<IAsyncSupport> async_support)
        :_async_support(std::move(async_support)),_h(std::move(h)),_valid(true) {}
    ///Destroy async socket
    ~AsyncSocket() {
        if (_valid) {
            _async_support->close(_h);
            std::destroy_at(&_h);
        }
    }
    ///Move
    AsyncSocket(AsyncSocket &&other)
        :_async_support(std::move(other._async_support))
        ,_valid(std::exchange(other._valid, false)) {
        if (_valid) {
            std::construct_at(&_h, std::move(other._h));
            std::destroy_at(&other._h);
        }
    }
    ///Move
    AsyncSocket &operator=(AsyncSocket &&other) {
        if (&other != this) {
            if (_valid) std::destroy_at(&_h);
            _valid = std::exchange(other._valid, false);
            std::construct_at(&_h,std::move(other._h));
            std::destroy_at(&other._h);
            _async_support = std::move(other._async_support);
        }
        return *this;
    }

    ///convert to socket handle
    operator SocketHandle() const {return _h;}
    ///test validity
    explicit operator bool() const {return _valid;}

    ///wait for stream input
    /**
     * @param handle handle of stream
     * @param timeout timeout
     * @retval true data arrived
     * @retval false timeout
     * @retval /canceled/ connection shutdown
     */
    coro::future<bool> input(std::chrono::system_clock::time_point timeout) {
        return _async_support->input(_h, timeout);
    }
    template<typename A, typename B>
    coro::future<bool> input(std::chrono::duration<A,B> timeout) {
        return input(std::chrono::system_clock::now()+timeout);
    }
    ///wait for stream output
    /**
     * @param handle handle of stream
     * @param timeout timeout
     * @retval true clear to send data
     * @retval false timeout
     * @retval /canceled/ connection shutdown
     */
    coro::future<bool> output(std::chrono::system_clock::time_point timeout) {
        return _async_support->output(_h, timeout);
    }

    template<typename A, typename B>
    coro::future<bool> output(std::chrono::duration<A,B> timeout) {
        return output(std::chrono::system_clock::now()+timeout);
    }
    ///Shutdown the connection
    void shutdown() {
        return _async_support->shutdown(_h);
    }
    ///Close connection - release handle
    void close() {
        return _async_support->close(_h);
    }
    ///Create timer
    /**
     * @param tp time when timer is triggered
     * @param ident optinal identification
     * @retval true time reached
     * @retval false timer canceled
     * @retval /canceled/ timer canceled because context is canceled
     */
    coro::future<bool> timer(std::chrono::system_clock::time_point tp, const void *ident) {
        return _async_support->timer(tp, ident);
    }
    ///Cancel time
    /**
     * @param ident identity of timer
     * @return object, which prevents timer creation while it is held.
     */
    auto cancel_timer(const void *ident) {
        return _async_support->cancel_timer(ident);
    }

    AsyncSocket set_handle(SocketHandle h) {
        return AsyncSocket(h, _async_support);
    }

protected:
    std::shared_ptr<IAsyncSupport> _async_support;
    union {
        SocketHandle _h;
    };
    bool _valid = false;
};


}


#endif
