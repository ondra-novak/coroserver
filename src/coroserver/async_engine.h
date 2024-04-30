#pragma once

#include <coro.h>

#include "peername.h"
#ifndef SRC_COROSERVER_ASYNC_ENGINE_H_
#define SRC_COROSERVER_ASYNC_ENGINE_H_


namespace coroserver {


class AsyncResource {};

class AsyncEngineImpl;


///asynchronous engine - single instance object, however, it is copyable (shared)
class AsyncEngine {
public:

    ///initailize engine
    AsyncEngine();
    ///remove one reference
    ~AsyncEngine();

    ///Connection handle
    using Handle = AsyncResource *;

    ///Return value of most operations
    /**
     * @retval -1 timeout
     * @retval 0 eof
     * @retval >0 count of processed bytes (1 for non-bytes result)
     * @retval /cancel/ when operation is blocked or canceled
     */
    using RetVal = coro::future<int>;
    ///Promise type
    using Promise = coro::promise<int>;
    ///Contains task to process when event is detected
    using Task = coro::promise<int>::notify;
    ///Timeout
    using Timepoint = std::chrono::system_clock::time_point;

    struct BlockingDeleter {
        Handle h;
        void operator()(AsyncEngine *ptr);
    };

    using Blocker = std::unique_ptr<AsyncEngine, BlockingDeleter>;

    ///Connect to remote network
    /**
     * @param target target address
     * @param timeout timeout point when operation expires
     * @return connection handle
     * @exception system_error any error including timeout.
     */
    coro::future<Handle> connect(const PeerName &target, Timepoint timeout);


    ///Listen for incomming connection
    /**
     * @param ifc interface name
     * @return a handle, which must be passed to accept to listen first connection
     */
    Handle listen(const PeerName &ifc);

    ///Recieve
    RetVal recv(Handle h, void *buffer, std::size_t size, Timepoint timeout);
    ///Send
    RetVal send(Handle h, const void *buffer, std::size_t size, Timepoint timeout);
    ///Accept incomming
    RetVal accept(Handle h, Handle &retHandle, PeerName &retPeerName, Timepoint timeout);
    void send_eof(Handle h);
    void close_handle(Handle h);
    Blocker block_async(Handle h);
    void shutdown(Handle h);

    ///retrieve length of output queue (at OS level);
    int get_siocoutq(Handle h);

    ///Waits for the next event to arrive, and then returns it.
    /**
     * blocks execution until next event arrives or timeout is reached. Note that
     * wait operation can be surplusly ended without any event. The caller must
     * repeat operation in this case
     *
     * @param timeout timeout, when wait ends
     *
     * @return Function returns task to execute, which is associated with the
     * event. If the task is not executed, it is executed automatically
     * in current thread during its destruction.
     * You can use if () to determine, whether the object
     * carries an executable task
     */
    Task wait_for_next_event(Timepoint timeout);
    ///Waits for the next event to arrive, and then returns it.
    /**
     * blocks execution until next event arrives. Note that
     * wait operation can be surplusly ended without any event. The caller must
     * repeat operation in this case
     *
     * @return Function returns task to execute, which is associated with the
     * event. If the task is not executed, it is executed automatically
     * in current thread during its destruction.
     * You can use if () to determine, whether the object
     * carries an executable task
     */
    Task wait_for_next_event();
    ///Cancels operation wait_for_next_even()
    /**
     * Causes that operation wait_for_next_event() is ended. It can still happen, that
     * operation is ended with a task to execute.
     *
     * If the function is called while there is nobody waiting, the
     * next call of wait_for_next_event() is ended immediatelly.
     *
     */
    void cancel_wait_for_next_event();

    ///block all asynchronous requests
    /**
     * @param block when true, async requests are blocked (they are canceled). If
     * false, async requests are enabled. When this value is true, all currently
     * pending requests are canceled (and cancel operation is carried in current thread)
     */
    void block_all(bool block);


protected:

    std::shared_ptr<AsyncEngineImpl> _ptr;


};



}




#endif /* SRC_COROSERVER_ASYNC_ENGINE_H_ */
