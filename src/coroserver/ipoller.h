/*
 * idispatcher.h
 *
 *  Created on: 27. 1. 2021
 *      Author: ondra
 */

#ifndef SRC_USERVER_IPOLLER_H_
#define SRC_USERVER_IPOLLER_H_

#include <cocls/future.h>


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



///Interface defines abstract dispatcher
/**
 * Dispatcher is single thread class, which monitors collection of asynchronous resources. You can
 * also define own type of asynchronous resource.
 *
 * The main goal is to monitor the collection, and when the particular resource is signaled, it
 * should return a task - structure which contains callback function and result of the monitoring.
 *
 * The task is later called.
 *
 * The monitoring is blocking, so during the operation, the thread is blocked
 *
 * @see getTask
 *
 */

template<typename SocketHandle>
class IPoller {
public:

    using Promise = cocls::promise_with_default_v<WaitResult, WaitResult::closed>;


    ///wait for read, resolve promise when done
    /**
     * @param op awaiting operation
     * @param s handle to monitor
     * @param p promise to resolve
     * @param timeout time point when timeout is reported
     */
    virtual void async_wait(AsyncOperation op, SocketHandle s, Promise p, std::chrono::system_clock::time_point timeout = std::chrono::system_clock::time_point::max()) = 0;

    ///timeout wait, use poller as scheduler
    /**
     * @param ident timer identifier
     * @param p promise to resolve
     * @param timeout time point when promise is resolved
     */
    virtual void schedule(const void *ident, Promise p, std::chrono::system_clock::time_point timeout)= 0;

    ///cancels specified timer
    /**
     * @param ident identifier of the timer
     * @retval true canceled
     * @retval false not found
     */
    virtual bool cancel_schedule(const void *ident) = 0;

    ///cancels any async IO operation, marks handle closing, but doesn't close it
    /**
     * @param s handle to mark close
     */

    virtual void mark_closing(SocketHandle s) = 0;

    ///cancels all async IO operations, marks all handles as closing
    /**
     * @note it also cancels all timeouts
     */
    virtual void mark_closing_all() = 0;

    ///notifies that handle has been closed
    /**
     * @param s handle to be closed
     *
     * @note the poller need to know, which handle has been closed to remove this
     * handle from the monitoring. Even if the handle is not monitored, it can still be
     * registered somewhere (for example marked for closing)
     */
    virtual void handle_closed(SocketHandle s) = 0;


	virtual ~IPoller() {}
};




}



#endif /* SRC_USERVER_IPOLLER_H_ */
