#pragma once

#ifndef SRC_COROSERVER_SOCKET_SUPPORT_
#define SRC_COROSERVER_SOCKET_SUPPORT_

#include "defs.h"
#include <coro.h>
#include <memory>


namespace coroserver {

enum class AsyncOperation {
    ///reading from stream
    read = 0,
    ///writing to stream
    write,
    ///accept new connection
    accept,
    ///connect to a server
    connect,
    ///end of waitable operations
    _count,
};


///Future contains result of wait operation
/**
 * Future can be resolved with true, when operation is complete
 *
 * Future can be resolved with false, when operation timeouted
 *
 * Future can be resolved with no-value. when connection has been closed
 *
 * Future can be resolved with an exception in case of error
 */
using WaitResult = coro::future<bool>;


void close_socket(const SocketHandle &handle);


class IAsyncSupport {
public:


    virtual ~IAsyncSupport() = default;
    virtual WaitResult io_wait(SocketHandle handle,
                     AsyncOperation op,
                     std::chrono::system_clock::time_point timeout) = 0;
    virtual void shutdown(SocketHandle handle) = 0;
    virtual void close(SocketHandle handle) = 0;
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

    ///Asynchronous wait
    /**
     * @param op async operation to wait
     * @retval true success
     * @retval false - timeout, never happen here
     * @retval no-value - connection shutdown
     * @exception any error detected on socket
     */
    coro::future<bool> io_wait(AsyncOperation op) {
        return io_wait(op, std::chrono::system_clock::time_point::max());
    }
    ///Asynchronous wait
    /**
     * @param op async operation to wait
     * @param timeout absolute time point when timeout happen. If the timepoint is
     * in past, it timeout immediatelly, however, if there is pending event it returns
     * success.
     * @retval true success
     * @retval false timeout
     * @retval no-value - connection shutdown
     * @exception any error detected on socket
     */
    WaitResult io_wait(AsyncOperation op, std::chrono::system_clock::time_point timeout) {
        return _async_support->io_wait(_h, op, timeout);
    }
    ///Asynchronous wait
    /**
     * @param op async operation to wait
     * @param dur timeout duration
     * @retval true success
     * @retval false timeout
     * @retval no-value - connection shutdown
     * @exception any error detected on socket
     */
    template<typename Rep, typename Period>
    WaitResult io_wait(AsyncOperation op, std::chrono::duration<Rep, Period> dur) {
        return io_wait(op, std::chrono::system_clock::now()+dur);
    }
    ///Shutdown connection
    /**
     * This cancels any async waiting, including any future requests for async waiting.
     * However, the socket is still valid, and can be used for communition, so
     * any unread data can be still read.
     *
     * @note cancel event is distributed in current thread, so any awaiting coroutine
     * is waken up now. The function returns after cancel is processed by all
     * awaiting coroutines
     */
    void shutdown() {
        _async_support->shutdown(_h);
    }

    AsyncSocket set_socket_handle(SocketHandle h) {
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
