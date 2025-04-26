#pragma once

#include "coroutines.hpp"
#include "timeout.hpp"
#include "stream_state.h"

namespace coroserver {

class Context;

class IStream {
public:

    virtual ~IStream() = default;

    virtual StreamState get_state() const  = 0;

    ///receive data asynchronously
    /**
     * @return string_view contains received data. It is always returned at least
     * one byte length data. If returned empty buffer, eof has been reached. If
     * timeout ellapses, async operation is canceled (returned no-value awaitable)
     *
     * @note the function is not concurrency safe. Only one coroutine can await on this method.
     * It is still posible to write during awaiting
     */
    [[nodiscard]] virtual coro::awaitable<std::string_view> read() = 0;
    ///put back some data to be received later
    /**
     * @param s a view contains data to put back. This should be part of data returned by
     * last receive(). You can put back a view to different view, but you must ensure that
     * underlying buffer remain valid until the data are retrieved. You can put_back only
     * one view, previous put view is replaced
     */
    virtual void put_back(std::string_view s) = 0;
    /// send buffer
    /**
     * @param data to send. Note the underlying buffer must remain valid
     * until the operation is complete. This must be handled well especially
     * when operation is performed asynchronously
     *
     * @retval true data successfully passed to the network stack for the delivery
     * @retval false stream has been closed or timeouted
     *
     * @note the function is not concurrency safe. Only one coroutine can await on this method.
     * It is still possible to read during awaiting
     */

    [[nodiscard]] virtual coro::awaitable<bool> write(std::string_view data) = 0;

    /// close the stream at output side
    /** Even if the stream is closed, there still can be unprocessed data.
     *  This function should change StreamState to closing
     *  @retval true stream has been closed by this function
     *  @retval false stream is in error state or already closed
     */
    [[nodiscard]] virtual coro::awaitable<bool> close() = 0;


    /// Shutdown asynchronous service for the stream.
    /**
     * Any peninng read is resolved with EOF
     *
     * Any pending write is resolved with FALSE
     **/
    virtual void shutdown() = 0;


    struct Counters {
        ///total received bytes
        std::size_t received;
        ///total sent bytes
        std::size_t sent;
    };

    ///Retrieve statistics counters
    virtual Counters get_counters() const  = 0;

    ///Retrieve current timeouts
    virtual IOTimeout get_timeouts() const = 0;

    ///Set new timeouts
    /**
     * Changed timeouts are applied immediately, but it also resets starting
     * point. If you need to cancel blocking operation, set timeout to zero
     * @param tm timeout structure
     */
    virtual void set_timeouts(IOTimeout tm) = 0;

    ///Retrieve asynchronous context associated with this object (if exists)
    /**
     * @return pointer to context. Note that this function can return nullptr
     * if no async context is associated
     */
    virtual Context get_context() const = 0;


};


}
