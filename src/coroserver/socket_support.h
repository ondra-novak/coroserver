#pragma once

#ifndef SRC_COROSERVER_SOCKET_SUPPORT_
#define SRC_COROSERVER_SOCKET_SUPPORT_

#include "defs.h"
#include <cocls/future.h>
#include <memory>


namespace coroserver {

enum class WaitResult {
    ///wait operation complete
    complete = 0,
    ///wait unsuccessful, timeout elapsed
    timeout,
    ///socket as been marked as closing
    closed,
    ///an error detected during processing
    error,

_count};    //contains count of states


enum class AsyncOperation {
    ///reading from stream
    read = 0,
    ///writing to stream
    write,
    ///accept new connection
    accept,
    ///connect to a server
    connect,

_count};  //contains count of states



class ISocketSupport {
public:

    virtual ~ISocketSupport() = default;
    virtual cocls::future<WaitResult> io_wait(SocketHandle handle,
                                             AsyncOperation op,
                                             std::chrono::system_clock::time_point timeout) = 0;
    virtual cocls::suspend_point<void> mark_closing(SocketHandle handle) = 0;
    virtual void close(SocketHandle handle) = 0;
    virtual cocls::future<WaitResult> wait_until(std::chrono::system_clock::time_point tp, const void *ident) = 0;
    virtual cocls::suspend_point<bool> cancel_wait(const void *ident) = 0;
};

/// Suppport context for sockets. (minimal interface)
/**
 *  Objects working with raw sockets should hold this object, which is count-ref pointer to support object. It
 *  allows to handle asynchronous operations
 */
class SocketSupport {
public:
    ///initialize object
    /**
     * @param ptr pointer to implementation.
     *
     * It is unlikely that you will need to construct object directly. In most cases, it is available as result
     * of function or method
     */
    SocketSupport(std::shared_ptr<ISocketSupport> ptr):_ptr(std::move(ptr)) {}

    ///Perform asynchronous waiting
    /**
     * @param handle socket handle which is being monitored for async operation.
     * @param op asynchronous operation to wait on
     * @param timeout time-point in a future, when the operation is finished as timeout. Use max() to infinity
     * waiting.
     * @return Function return future which is resolved with one of WaitResult.
     *
     * @note Compound value handle-op is considered as unique key to a map of pending operations. Calling this
     * function with duplicated key is UB.
     */
    cocls::future<WaitResult> io_wait(SocketHandle handle,
                                      AsyncOperation op,
                                      std::chrono::system_clock::time_point timeout) {
                                        return _ptr->io_wait(handle, op, timeout);
                                      }

    /// Marks socket as closing
    /**
     *  This operation affect any pending and further waiting. Any currently pending or future waiting is immediately
     *  resolved with result WaitResult::closed. It is considered as closed connection regadless on state of the connection. Function
     *  allows to unblock any pending operation when stream is being closed prematurely.
     *
     *  @note Any pending coroutines are prepared in returned suspend_point
     */
    cocls::suspend_point<void> mark_closing(SocketHandle handle) {
         return _ptr->mark_closing(handle);
    }
    /// Closes the socket
    /**
     *  Do not call standard syscall ::close() on the socket. You need to call this function to close the socket. It also
     *  releases any remaining resources associated with the socked, that could be created to handle asynchronous operations
     *
     *  @note Ensure, that there is no pending operation before the socket is closed. You should call mark_closing() and
     *  join any coroutines that can work with the handle.
     *
     */
    void close(SocketHandle handle) {
        _ptr->close(handle);
    }
    ///Wait until specified timepoint is reached
    /**
     * This is just generic "sleep" helps to implement user level timeouts. The promise
     * becomes resolved once the timepoint is reached. The waiting can be also canceled
     * @param tp timepoint to reach
     * @param ident identifier, to ability to cancel such wait. Can be any arbitrary
     * value serves as identifier. It is presented as pointer as it is expected, that
     * pointer to owner will be used as identifier (this)
     * @return future which resolves one of following values
     * @retval WaitResult::timeout operation reached specified timeout
     * @retval WaitResult::complete operation has been canceled (cancel request is complete)
     * @retval WaitResult::closed operation has been canceled because context has been stopped
     */
    cocls::future<WaitResult> wait_until(std::chrono::system_clock::time_point tp, const void *ident) {
        return _ptr->wait_until(tp,ident);;
    }
    ///Wait for specified duration
    /**
     * This is just generic "sleep" helps to implement user level timeouts. The promise
     * becomes resolved once the timepoint is reached. The waiting can be also canceled
     * @param duration duration. The final timeout is calculated as now()+duration
     * @param ident identifier, to ability to cancel such wait. Can be any arbitrary
     * value serves as identifier. It is presented as pointer as it is expected, that
     * pointer to owner will be used as identifier (this)
     * @return future which resolves one of following values
     * @retval WaitResult::timeout operation reached specified timeout
     * @retval WaitResult::complete operation has been canceled (cancel request is complete)
     * @retval WaitResult::closed operation has been canceled because context has been stopped
     */
    template<typename Dur>
    cocls::future<WaitResult> wait_for(Dur duration, const void *ident) {
        return wait_until(std::chrono::system_clock::now()+duration, ident);
    }
    ///Cancels specified waiting
    /**
     * @param ident identifier of operation. The canceled waiting is resolved as WaitResult::closed
     * @retval true found and canceled
     * @retval false not found, probably complete already
     */
    cocls::suspend_point<bool> cancel_wait(const void *ident) {
        return _ptr->cancel_wait(ident);
    }

protected:
    std::shared_ptr<ISocketSupport> _ptr;

};


}


#endif
