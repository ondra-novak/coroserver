#pragma once

#include <coro.h>

#include "peername.h"
#ifndef SRC_COROSERVER_ASYNC_ENGINE_H_
#define SRC_COROSERVER_ASYNC_ENGINE_H_


namespace coroserver {


class AsyncResource {};

class AsyncEngineImpl;


enum class SpecialDevice {
    std_input,
    std_output,
    std_error,
};

enum class OperationMode {
    read,
    write,
    bidirectional
};


///asynchronous engine - single instance object, however, it is copyable (shared)
class AsyncEngine {
public:

    ///initailize engine
    AsyncEngine();
    ///remove one reference
    ~AsyncEngine();

    ///Connection handle
    using Handle = AsyncResource *;

    struct UniqueHandleDeleter {
        std::shared_ptr<AsyncEngineImpl> _ptr;
        UniqueHandleDeleter(std::shared_ptr<AsyncEngineImpl> ptr):_ptr(std::move(ptr)) {}

        void operator()(AsyncResource *h);
    };

    using UniqueHandle = std::unique_ptr<AsyncResource, UniqueHandleDeleter>;

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
     * @param cancel a stop token which allows to cancel operation.
     * @return connection handle
     * @exception system_error any error including timeout.
     * @exception await_canceled_exception if token is used to cancel operation
     */
    coro::future<UniqueHandle> connect(const PeerName &target, Timepoint timeout, std::stop_token cancel);


    ///Listen for incomming connection
    /**
     * @param ifc interface name
     * @return a handle, which must be passed to accept to listen first connection
     */
    UniqueHandle listen(const PeerName &ifc);

    static AsyncEngine get_engine(const UniqueHandle &h);

    ///Recieve
    RetVal recv(Handle h, void *buffer, std::size_t size, Timepoint timeout);
    int recv_nb(Handle h, void *buffer, std::size_t size);

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

    PeerName get_name(Handle h);

    ///connect special device
    UniqueHandle connect_special(SpecialDevice dev);
    ///connect named pipe
    /**
     * @param mode open mode
     * @param name name of pipe
     * @param timeout timeout to wait
     * @param cancel stop token, useful to cancel operation
     * @return handle
     *
     * connects named pipe and waits for other side. If the named pipe doesn't exists,
     * it is created. If name doesn't start with dot or slash, the named pipe is created
     * in default directory. If you need to create named pipe in the current directory, type
     *  "./name" with "./" as prefix
     *
     */
    coro::future<UniqueHandle> connect_named_pipe(OperationMode mode, const std::string &name, Timepoint timeout, std::stop_token cancel);
    ///Create connection to new or existing file
    /**
     * @param mode operation mode
     * @param name name of file
     * @param append if true, and OperationMode is write, then new data are appended. If false
     * then file is truncated. The flag is ignored while OperationMode is read
     *
     * You can open named pipe by this function
     *
     * @return handle
     */
    UniqueHandle open_file(OperationMode mode, const std::string &name, bool append = false);

    ///Opens SIGINT signal listener.
    /**
     * This creates a virtual pipe/file which sends some bytes when interrupt signal is
     * received. This signal covers SIGINT, SIGTERM, SIGQUIT, SIGHUP and etc. Undef Windows
     * this signal is implemented by SetConsoleCtrlHandler.
     * @return handle which is pipe
     *
     * @note you can retrieve multiple handles by this call, but only one
     * can receive signal.
     */
    UniqueHandle open_intr_signal_listener();

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

    AsyncEngine(std::shared_ptr<AsyncEngineImpl> ptr);

    std::shared_ptr<AsyncEngineImpl> _ptr;
};






}




#endif /* SRC_COROSERVER_ASYNC_ENGINE_H_ */
