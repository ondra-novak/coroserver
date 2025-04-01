#pragma once
#include "string_search.h"
#include "istream.hpp"

namespace coroserver{

class ReceiveBlockStatus {
public:
    ReceiveBlockStatus(bool st):_st(st) {};
    operator bool() const {return _st;}

protected:
    bool _st;
};



template<typename Cont>
class ReceiveUntilState {
public:

    ReceiveUntilState(Cont &buff,
            pattern_search<char> &&patt,
            std::size_t maxbuff,
            std::shared_ptr<IStream> stream)
        :_buff(buff)
        ,_patt(std::move(patt))
        ,_maxbuff(maxbuff)
        ,_stream(std::move(stream)) {}


    coro::prepared_coro operator()(coro::awaitable_result<ReceiveBlockStatus> promise) {
        return _callback.await(_stream->read(), [this,promise = std::move(promise)](auto &awt) mutable {
            return process_data(awt, promise);
        });
    }

protected:
    Cont &_buff;
    pattern_search<char> _patt;
    std::size_t _maxbuff;
    std::shared_ptr<IStream> _stream;

    coro::prepared_coro process_data(coro::awaitable<std::string_view> &awt, coro::awaitable_result<ReceiveBlockStatus> &promise) {
        {
           try {
               std::string_view data = awt.await_resume();
               if (data.empty()) {
                   return promise(false);
               }
               for (std::size_t i = 0; i < data.size(); ++i) {
                   if (_patt(data[i])) {
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
               _callback.await_cont(_stream->read());
               return {};
           } catch (...) {
               return promise.set_exception(std::current_exception());
           }
       }
    }
    coro::awaiting_callback<coro::awaitable<std::string_view>, ReceiveUntilState *, coro::awaitable_result<ReceiveBlockStatus>> _callback;
};

}

template<>
struct coro::awaitable_reserved_space<coroserver::ReceiveBlockStatus> {
    static constexpr std::size_t value = sizeof(coroserver::ReceiveUntilState<std::vector<char> >);
};

namespace coroserver {


template<typename Cont>
class ReceiveBlockState {
public:

    ReceiveBlockState(Cont &buff,
            std::size_t maxbuff,
            std::shared_ptr<IStream> stream)
        :_buff(buff)
        ,_maxbuff(maxbuff)
        ,_stream(std::move(stream)) {}

        void operator()(coro::awaitable_result<ReceiveBlockStatus> promise) {
        _callback.await(_stream->read(),[this, promise = std::move(promise)](coro::awaitable<std::string_view> &awt) mutable {
            return process_data(awt, promise);
        });
    }

protected:
    Cont &_buff;
    std::size_t _maxbuff;
    std::shared_ptr<IStream> _stream;

    coro::prepared_coro process_data(coro::awaitable<std::string_view> &awt,
            coro::awaitable_result<ReceiveBlockStatus> &promise) {
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
            _callback.await_cont(_stream->read());
            return {};
        } catch (...) {
            return promise.set_exception(std::current_exception());
        }
    }

    coro::awaiting_callback<coro::awaitable<std::string_view>, ReceiveBlockState *,
                coro::awaitable_result<ReceiveBlockStatus> > _callback;
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
     * @return An awaitable string view containing the readd data.
     * If the function returns an empty string, you should check the stream's
     * state using `get_state()`.
     *
     * Possible stream states:
     * - `StreamState::closed`: The stream was closed by the other side (EOF reached).
     * - Other states: The read operation was interrupted, likely due to a timeout.
     *
     * Example usage:
     * @code
     * std::string_view data = co_await stream.read();
     * if (data.empty() && stream.get_state() == StreamState::closed) {
     *     std::cout << "Stream closed.\n";
     * }
     * @endcode
     */
    [[nodiscard]] coro::awaitable<std::string_view> read() {
        return _ptr->read();
    }
    /// Push data back into the stream for re-reading.
    /**
     * This function allows returning part of a previously readd buffer
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
     * std::string_view data = co_await stream.read();
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
    [[nodiscard]] coro::awaitable<bool> write(std::string_view data) {
        return _ptr->write(data);
    }
    ///Mark stream closed
    /**
     * This function marks stream closed. Note that stream is closed
     * once all data are sent. Before the stream is fully closed, all
     * incoming data must be also processed.
     *
     * This function writes close to other side, the other side reads EOF.
     * The other side must close its side to full close the stream.
     *
     * You can also destroy the stream, which can cause that data will not
     * be delivered. Always perform cooperative close with the other side
     * to prevent data lost.
     */
    [[nodiscard]] coro::awaitable<bool> close() {
        return _ptr->close();
    }

    void shutdown() {
        return _ptr->shutdown();
    }

    ///tests, whether stream is initialized
    explicit operator bool() const {return static_cast<bool>(_ptr);}

    using Status = ReceiveBlockStatus;

    ///reads until separator is reached,
    /**
     * @param buffer a container which reads data.
     * @param sep separator
     * @param limit maximum size. This value is to reject any stream with
     * unexpectedly longer lines. Reaching this limit is considered as an error. Default
     * value means no limit
     * @retval true readd successfully
     * @retval false error - limit reached, eof reached, timeout. The already read
     * data are still stored in the buffer.
     */
    template<typename Cont>
    coro::awaitable<Status> read_until(Cont &buffer, std::string_view pattern, size_t limit = ~static_cast<std::size_t>(0)) {
        pattern_search<char> patt(pattern);
        buffer.clear();
        auto awt = _ptr->read();
        if (awt.is_ready()) {
            std::string_view z = awt.await_resume();
            for (std::size_t i = 0; i < z.size(); ++i) {
                if (patt(z[i])) {
                    ++i;
                    auto sub = z.substr(0,i-patt.size());
                    std::copy(sub.begin(), sub.end(), std::back_inserter(buffer));
                    z = z.substr(i);
                    _ptr->put_back(z);
                    return true;
                }
            }
            std::copy(z.begin(), z.end(), std::back_inserter(buffer));
        }
        awt.cancel();
        return ReceiveUntilState<Cont>(buffer, std::move(patt), limit, _ptr);

    }


    ///read block
    /**
     * @param buffer container receiving the data
     * @param size size of block
     * @retval true readd
     * @retval false reached error or timeout. The already readd data
     * are placed to the buffer
     */
    template<typename Cont>
    coro::awaitable<Status> read_block(Cont &buffer, size_t size) {
        buffer.clear();
        auto awt = _ptr->read();
        if (awt.is_ready()) {
            std::string_view z = awt.await_resume();
            if (z.size() >= size) {
                auto sub = z.substr(0,size);
                _ptr->put_back(z.substr(size));
                std::copy(sub.begin(), sub.end(), std::back_inserter(buffer));
                return true;
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

    auto get_handle() const {return _ptr;}

    Context get_context() const;
protected:
    std::shared_ptr<IStream> _ptr;
};


class StreamProxy: public IStream {
public:

    StreamProxy(Stream s):_s(std::move(s)) {}

    virtual StreamState get_state() const override {return _s.get_state();}
    virtual coro::awaitable<std::string_view> read() override {return _s.read();}
    virtual void put_back(std::string_view s) override {return _s.put_back(s);}
    virtual coro::awaitable<bool> write(std::string_view data) override {return _s.write(data);}
    virtual coro::awaitable<bool> close() override {return _s.close();}
    virtual Counters get_counters() const override {return _s.get_counters();}
    virtual IOTimeout get_timeouts() const override {return _s.get_timeouts();}
    virtual void set_timeouts(IOTimeout tm) override {_s.set_timeouts(tm);}
    virtual Context get_context() const override;
    virtual void shutdown() override {_s.shutdown();}
protected:
    Stream _s;


};

}
