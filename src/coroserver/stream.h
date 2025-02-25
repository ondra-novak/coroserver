#pragma once
#include "coroutines.h"
#include "string_search.h"
#include "timeout.h"

namespace coroserver{

class INetContext;

/// Represents the possible states of a stream (e.g., network connection, file stream, etc.).
enum class StreamState {
    /// The stream is in the process of being opened but is not yet fully available.
    opening,
    /// The stream is open and ready for reading and/or writing operations.
    active,
    /// The stream is in the process of closing; data may still be available for reading.
    closing,
    /// The stream is fully closed; no further input or output operations are possible.
    closed
};
class IStream {
public:

    virtual ~IStream() = default;

    virtual StreamState get_state() const  = 0;

    ///receive data asynchronously
    /**
     * @return string_view contains received data. It is always returned at least
     * one byte length data. If returned empty buffer, timeout or eof has been reached.
     * Use is_eof() to determine what happened
     *
     * @note the function is not concurrency safe. Only one coroutine can await on this method.
     * It is still posible to write during awaiting
     */
    [[nodiscard]] virtual awaitable<std::string_view> receive() = 0;
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
     * @retval false stream has been closed
     *
     * @note the function is not concurrency safe. Only one coroutine can await on this method.
     * It is still possible to read during awaiting
     */

    [[nodiscard]] virtual awaitable<bool> send(std::string_view data) = 0;

    /// close the stream at output side
    /** Even if the stream is closed, there still can be unprocessed data.
     *  This function should change StreamState to closing
     *  @retval true stream has been closed by this function
     *  @retval false stream is in error state or already closed
     */
    [[nodiscard]] virtual awaitable<bool> close() = 0;


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
    virtual std::shared_ptr<INetContext> get_async_context() const = 0;


};

template<typename Cont, unsigned int n>
class ReceiveUntilStatus;
template<typename Cont, unsigned int n>
class ReceiveUntilState {
public:

    ReceiveUntilState(Cont &buff,
            pattern_search<char,n> patt,
            typename pattern_search<char,n>::state stat,
            std::size_t maxbuff,
            std::shared_ptr<IStream> stream)
        :_buff(buff)
        ,_patt(patt)
        ,_stat(stat)
        ,_maxbuff(maxbuff)
        ,_stream(std::move(stream)) {}

    void operator()(awaitable_result<ReceiveUntilStatus<Cont,n> > promise) {
        _callback.await(_stream->receive(), this, std::move(promise));
    }

    void operator()(awaitable_result<int>); //just make compiler happy

protected:
    Cont &_buff;
    pattern_search<char,n> _patt;
    typename pattern_search<char,n>::state _stat;
    std::size_t _maxbuff;
    std::shared_ptr<IStream> _stream;

    prepared_coro process_data(awaitable<std::string_view> &awt, awaitable_result<ReceiveUntilStatus<Cont, n> > &promise) {
        try {
            std::string_view data = awt.await_resume();
            if (data.empty()) {
                return promise(false);
            }
            for (std::size_t i = 0; i < data.size(); ++i) {
                if (_patt.test(data[i],_stat)) {
                    ++i;
                    if (i > _patt.size()) {
                        auto sub = data.substr(0, i  - _patt.size());
                        std::copy(sub.begin(), sub.end(), std::back_inserter(_buff));
                    } else {
                        auto extra = _patt.size() - i;
                        while (extra) {
                            _buff.pop_back();
                            --extra;
                        }
                    }
                    auto remain = data.substr(i);
                    _stream->put_back(remain);
                    return promise(true);
                }
            }
            std::copy(data.begin(), data.end(), std::back_inserter(_buff));
            if (_buff.size() > _maxbuff) {
                return promise(false);
            }
            _callback.await_cont(_stream->receive());
            return {};
        } catch (...) {
            return promise.set_exception(std::current_exception());
        }
    }

    await_member_callback<std::string_view, ReceiveUntilState *,
        &ReceiveUntilState::process_data, awaitable_result<ReceiveUntilStatus<Cont, n> >> _callback;
};

///Holds status of receive_until operation of Stream
/**
 * This object can be converted to bool status. Status true means
 * success, status false means true
 *
 */
template<typename Cont, unsigned int n>
class ReceiveUntilStatus {
public:
    ReceiveUntilStatus(bool st) {
        buffer[0] = st?1:0;
    }
    operator bool() const {
        return buffer[0] != 0;
    }

protected:
    char buffer[sizeof(awaitable<int>::CallbackImpl<ReceiveUntilState<Cont,n> >)];
};


template<typename Cont>
class ReceiveBlockStatus;
template<typename Cont>
class ReceiveBlockState {
public:

    ReceiveBlockState(Cont &buff,
            std::size_t maxbuff,
            std::shared_ptr<IStream> stream)
        :_buff(buff)
        ,_maxbuff(maxbuff)
        ,_stream(std::move(stream)) {}

    void operator()(awaitable_result<ReceiveBlockStatus<Cont> > promise) {
        _callback.await(_stream->receive(), this, std::move(promise));
    }

    void operator()(awaitable_result<int>); //just make compiler happy

protected:
    Cont &_buff;
    std::size_t _maxbuff;
    std::shared_ptr<IStream> _stream;

    prepared_coro process_data(awaitable<std::string_view> &awt,
            awaitable_result<ReceiveBlockStatus<Cont> > &promise) {
        try {
            std::string_view data = awt.await_resume();
            if (data.empty()) {
                return promise(false);
            }
            std::size_t remain = _maxbuff - _buff.size();
            if (remain <= data.size()) {
                auto a = data.substr(0,remain);
                auto b = data.substr(remain);
                std::copy(a.begin(),a.end(), std::back_inserter(_buff));
                _stream->put_back(b);
                return promise(true);
            }
            std::copy(data.begin(), data.end(), std::back_inserter(_buff));
            _callback.await_cont(_stream->receive());
            return {};
        } catch (...) {
            return promise.set_exception(std::current_exception());
        }
    }

    await_member_callback<std::string_view, ReceiveBlockState *,
        &ReceiveBlockState::process_data, awaitable_result<ReceiveBlockStatus<Cont> > >
            _callback;
};

///Holds status of receive_block operation of Stream
/**
 * This object can be converted to bool status. Status true means
 * success, status false means true
 *
 */
template<typename Cont>
class ReceiveBlockStatus {
public:
    ReceiveBlockStatus(bool st) {
        buffer[0] = st?1:0;
    }
    operator bool() const {
        return buffer[0] != 0;
    }

protected:
    char buffer[sizeof(awaitable<int>::CallbackImpl<ReceiveBlockState<Cont> >)];
};



class Stream {
public:

    Stream() = default;
    Stream(std::shared_ptr<IStream> ptr):_ptr(std::move(ptr)) {}

    ///Retrieve stream state
    /**
     * @return StreamState
     */
    StreamState get_state() const {
        return _ptr->get_state();
    }

    /// Receive data from the stream.
    /**
     * This function asynchronously reads at least one byte from the stream.
     * If no data is available, the function blocks (or suspends a coroutine)
     * until data arrives or a timeout occurs.
     *
     * @return An awaitable string view containing the received data.
     * If the function returns an empty string, you should check the stream's
     * state using `get_state()`.
     *
     * Possible stream states:
     * - `StreamState::closed`: The stream was closed by the other side (EOF reached).
     * - Other states: The read operation was interrupted, likely due to a timeout.
     *
     * Example usage:
     * @code
     * std::string_view data = co_await stream.receive();
     * if (data.empty() && stream.get_state() == StreamState::closed) {
     *     std::cout << "Stream closed.\n";
     * }
     * @endcode
     */
    [[nodiscard]] awaitable<std::string_view> receive() {
        return _ptr->receive();
    }
    /// Push data back into the stream for re-reading.
    /**
     * This function allows returning part of a previously received buffer
     * back into the stream so that it will be read again on the next read operation.
     *
     * @param s A string view that should ideally be a direct sub-view of the
     *          previously returned buffer.
     *
     * The function can be used to reprocess already read data. Although it is
     * possible to return an entirely different string, the caller must ensure that
     * the underlying buffer remains valid until the next read operation.
     *
     * Important notes:
     * - This function can only be called once before the next read operation.
     * - Calling it multiple times will overwrite the previously stored view.
     *
     * Example usage:
     * @code
     * std::string_view data = co_await stream.receive();
     * if (data.size() > 5) {
     *     process_data(data.substr(0, 5));  // Process only the first 5 bytes
     *     stream.put_back(data.substr(5));  // Return the remaining part
     * }
     * @endcode
     */
    void put_back(std::string_view s){
        _ptr->put_back(s);
    }
    /// Send data to the stream asynchronously.
    [[nodiscard]] awaitable<bool> send(std::string_view data) {
        return _ptr->send(data);
    }
    ///Mark stream closed
    /**
     * This function marks stream closed. Note that stream is closed
     * once all data are sent. Before the stream is fully closed, all
     * incoming data must be also processed.
     *
     * This function sends close to other side, the other side receives EOF.
     * The other side must close its side to full close the stream.
     *
     * You can also destroy the stream, which can cause that data will not
     * be delivered. Always perform cooperative close with the other side
     * to prevent data lost.
     */
    [[nodiscard]] awaitable<bool> close() {
        return _ptr->close();
    }


    ///tests, whether stream is initialized
    explicit operator bool() const {return static_cast<bool>(_ptr);}

    ///reads until separator is reached,
    /**
     * @param buffer a container which receives data.
     * @param sep separator
     * @param limit maximum size. This value is to reject any stream with
     * unexpectedly longer lines. Reaching this limit is considered as an error. Default
     * value means no limit
     * @retval true received successfully
     * @retval false error - limit reached, eof reached, timeout. The already read
     * data are still stored in the buffer.
     */
    template<typename Cont, unsigned int n>
    [[nodiscard]] awaitable<ReceiveUntilStatus<Cont, n> > receive_until(Cont &buffer, pattern_search<char, n> patt, size_t limit = ~static_cast<std::size_t>(0)) {
        buffer.clear();
        auto awt = _ptr->receive();
        auto state = patt.begin_search();
        if (awt.is_ready()) {
            std::string_view z = awt.await_resume();
            for (std::size_t i = 0; i < z.size(); ++i) {
                if (patt.test(z[i],state)) {
                    ++i;
                    auto sub = z.substr(0,i-patt.size());
                    std::copy(sub.begin(), sub.end(), std::back_inserter(buffer));
                    z = z.substr(i);
                    _ptr->put_back(z);
                    return ReceiveUntilStatus<Cont, n>(true);
                }
            }
            std::copy(z.begin(), z.end(), std::back_inserter(buffer));
        }
        awt.cancel();
        return ReceiveUntilState<Cont, n>(buffer, patt, state, limit, _ptr);

    }

    template<typename Cont, unsigned int n>
    [[nodiscard]] awaitable<ReceiveUntilStatus<Cont, n> > receive_until(Cont &buffer, const char (&sep)[n], size_t limit = ~static_cast<std::size_t>(0)) {        ;
        return receive_until(buffer, pattern_search<char, n>(sep), limit);

    }


    ///receive block
    /**
     * @param buffer container receiving the data
     * @param size size of block
     * @retval true received
     * @retval false reached error or timeout. The already received data
     * are placed to the buffer
     */
    template<typename Cont>
    awaitable<ReceiveBlockStatus<Cont> > receive_block(Cont &buffer, size_t size) {
        buffer.clear();
        auto awt = _ptr->receive();
        if (awt.is_ready()) {
            std::string_view z = awt.await_resume();
            if (z.size() >= size) {
                auto sub = z.substr(0,size);
                _ptr->put_back(z.substr(size));
                std::copy(sub.begin(), sub.end(), std::back_inserter(buffer));
                return ReceiveBlockStatus<Cont>(true);
            }
            std::copy(z.begin(), z.end(), std::back_inserter(buffer));
        }
        awt.cancel();
        return ReceiveBlockState<Cont>(buffer, size, _ptr);
    }

    using Counters = IStream::Counters;

    Counters get_counters() const  {return _ptr->get_counters();}
    IOTimeout get_timeouts() const  {return _ptr->get_timeouts();}
    void set_timeouts(IOTimeout tm)   {return _ptr->set_timeouts(tm);}

    std::shared_ptr<INetContext> get_async_context() const {return _ptr->get_async_context();}
protected:
    std::shared_ptr<IStream> _ptr;
};


class StreamProxy: public IStream {
public:

    StreamProxy(Stream s):_s(std::move(s)) {}

    virtual StreamState get_state() const override {return _s.get_state();}
    virtual awaitable<std::string_view> receive() override {return _s.receive();}
    virtual void put_back(std::string_view s) override {return _s.put_back(s);}
    virtual awaitable<bool> send(std::string_view data) override {return _s.send(data);}
    virtual awaitable<bool> close() override {return _s.close();}
    virtual Counters get_counters() const override {return _s.get_counters();}
    virtual IOTimeout get_timeouts() const override {return _s.get_timeouts();}
    virtual void set_timeouts(IOTimeout tm) override {_s.set_timeouts(tm);}
    virtual std::shared_ptr<INetContext> get_async_context() const override {return _s.get_async_context();}
protected:
    Stream _s;


};

}
