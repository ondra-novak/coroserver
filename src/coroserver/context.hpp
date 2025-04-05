#pragma once

#include <chrono>
#include <memory>
#include <span>
#include "coroutines.h"
#include "stream_state.h"

namespace coroserver {


class ContextImpl;
class Environment;


enum class SpecialDevice {
    standard_input,
    standard_output,
    standard_error
};

class Context {
public:

    using Handle = std::size_t;

    static constexpr Handle null_handle = 0;

    ///Default constructor is deleted to avoid errors
    Context() = delete;

    Context(std::shared_ptr<ContextImpl>);
    ~Context();

    ///create server, you can use accept() on the handle
    /**
     * @param host host, where to create server, use '*' as all interfaces. Use : to override
     *  port, for example localhost:1234, *:1234.
     * @param def_port default port. If you specify empty value, random port is selected
     * @return handle to server
     */
    Handle create_server(std::string host, std::string def_port);

    ///connect to remote server
    /**
     * @param host host, can contain port to override default port
     * @param def_port default port if not specified in host
     * @return handle to stream (receive, send)
     */
    Handle connect(std::string host, std::string def_port);


    Handle create_process(std::string_view path, std::span<const std::string_view> argv,  const Environment & envp);

    ///Terminate process created by create_process
    /**
     * @param h handle to process created by create_process
     * @retval true terminated
     * @retval false invalid handle, process exited etc
     *
     * @note the process is force-terminated (TerminateProcess, kill sigkill)
     *
     */
    bool terminate_process(Handle h);

    coro::awaitable<int> get_process_exit_status(Handle h, std::chrono::system_clock::time_point tp);


    Handle connect_stdinout();


    ///creates timer handle, it can be used by function sleep
    Handle create_timer();

    ///close the handle, any pending operation is canceled
    void close(Handle h);

    ///retrieve host of the handle
    std::string get_host(Handle h) const;

    ///sleep on the timer
    /**
     * @param timer timer handle, must be create by create_timer
     * @param tp time point to sleep until
     * @return awaitable
     * @retval true sleep success
     * @retval false sleep interrupted
     */
    coro::awaitable<bool> sleep(Handle timer, std::chrono::system_clock::time_point tp);

    ///accept one incomming connection
    /**
     * @param server server handle
     * @param timeout timeout time point
     * @return awaitable which return valid handle if accepted connection, returns 0 if
     *   operation was canceled and returns no-value if timeout
     */
    coro::awaitable<Handle> accept(Handle server, std::chrono::system_clock::time_point timeout);

    ///receive data from stream
    /**
     * @param stream stream handle
     * @param buffer pointer to buffer
     * @param sz size of the buffer must be larger than zero
     * @param timeout timeout when operation expires
     * @return awaitable
     * @retval >0 count of received bytes
     * @retval =0 eof reached
     * @retval no-value timeout
     *
     * @note only one receive at time is allowed
     */
    coro::awaitable<size_t> receive(Handle stream, char *buffer, std::size_t sz, std::chrono::system_clock::time_point timeout);

    ///send data to stream
    /**
     * @param stream stream handle
     * @param buffer pointer to data to send
     * @param sz size of data to send
     * @param timeout timeout
     * @return awaitable
     * @retval true all data sent
     * @retval false error/timeout/connection is lost. In this state, there is no way
     * to find out, how many bytes has been sent. They can be all lost.
     *
     * @note only one send or send_eof at time is allowed
     */
    coro::awaitable<bool> send(Handle stream, const char *buffer, std::size_t sz, std::chrono::system_clock::time_point timeout);

    ///send EOF to indicate end of stream
    /**
     * @param stream stream handle
     * @return awaitable
     * @retval true eof sent, ouput stream is closed
     * @retval false stream is in error state, already closed etc
     *
     * @note only one send or send_eof at time is allowed
     */
    coro::awaitable<bool> send_eof(Handle stream);

    ///retrieves state of the stream
    /**
     * @param h handle. It should be stream handle, but server handle should work too
     * @retval opening - opening state, typically for connecting stream, when connection is not
     * yet established
     * @retval active - connection is established
     * @retval closing - connection has been partially closed by send_eof.
     * @retval closed - connection is shutdown or closed, or invalid handle has been specified
     */
    StreamState get_state(Handle h) const;


    ///Shutdown the handle
    /**
     * Switch handle to shutdown mode. Any blocking operation is immediatelly canceled. Any
     * pending operation is canceled. Pending receive is returned with 0. Pending send
     * is returned with false, Pending sleep is returned with false, Pending accept is returned
     * with null handle.
     *
     * The shutdown state is final. Any futher blocking operation on handle is rejected. The
     * handle must be closed.
     * @param h handle to shutdown
     *
     * @note pending coroutines are executed on internal thread asynchronously.
     */
    void shutdown(Handle h);


    ///Create IO context
    /**
     * @param iothreads count of IO threads
     * @return context
     *
     * @note context is ref-count shared resource. It is destroyed when all
     * references are released
     */
    static Context create(unsigned int iothreads = 1);


protected:
    std::shared_ptr<ContextImpl> _impl;

};

}

