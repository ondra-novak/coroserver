#include "coroutines.h"
#include "string_search.h"

namespace coroserver{

class IStream {
public:
    virtual ~IStream() = default;
    ///receive data asynchronously
    /**
     * @return string_view contains received data. It is always returned at least
     * one byte length data. If returned empty buffer, timeout or eof has been reached.
     * Use is_eof() to determine what happened
     *
     * @note the function is not concurrency safe. Only one coroutine can await on this method.
     * It is still posible to write during awaiting
     */
    virtual awaitable<std::string_view> receive() = 0;
    ///put back some data to be received later
    /**
     * @param s a view contains data to put back. This should be part of data returned by
     * last receive(). You can put back a view to different view, but you must ensure that
     * underlying buffer remain valid until the data are retrieved. You can put_back only
     * one view, previous put view is replaced
     */
    virtual void put_back(std::string_view s) = 0;
    ///returns true, if eof has been reached by last read
    /**
     * @retval true last read failed because eof
     * @retval false last read didn't failed or failed because timeout
     */
    virtual bool is_eof() const = 0;
    /// send buffer
    /**
     * @param data to send
     * @retval true data successfuly left output buffer to networ
     * @retval false data has been discarded, because network error (this also closes the connection)
     * @note you can discard awaitable object. The function should always send the buffer, but by
     * discarding awaitable also discard status of the connection.
     * @note the function can immediately return false if connection is already closed
     *
     * @note the function is concurrency safe. Multiple coroutines can await
     */

    virtual awaitable<bool> send(std::string_view data) = 0;

    /// Sends eof and closes outgoing connection
    virtual void send_eof() = 0;


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
        _promise = std::move(promise);
        _stream->receive().set_callback(CB{this}, _callback_buffer);
    }

    void operator()(awaitable_result<int>); //just make compiler happy

protected:
    Cont &_buff;
    pattern_search<char,n> _patt;
    typename pattern_search<char,n>::state _stat;
    std::size_t _maxbuff;
    awaitable_result<ReceiveUntilStatus<Cont, n> > _promise;
    std::shared_ptr<IStream> _stream;

    struct CB {
        ReceiveUntilState *owner;
        void operator()(awaitable<std::string_view> &awt) {
            owner->process_data(awt);
        }
    };

    char _callback_buffer[sizeof(awaiting_callback<std::string_view, CB>)];

    prepared_coro process_data(awaitable<std::string_view> &awt) {
        try {
            std::string_view data = awt.await_resume();
            if (data.empty()) {
                return _promise(false);
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
                    return _promise(true);
                }
            }
            std::copy(data.begin(), data.end(), std::back_inserter(_buff));
            if (_buff.size() > _maxbuff) {
                return _promise(false);
            }
            _stream->receive().set_callback(CB{this}, _callback_buffer);
            return {};
        } catch (...) {
            return _promise.set_exception(std::current_exception());
        }
    }
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
        _promise = std::move(promise);
        _stream->receive().set_callback(CB{this}, _callback_buffer);
    }

    void operator()(awaitable_result<int>); //just make compiler happy

protected:
    Cont &_buff;
    std::size_t _maxbuff;
    awaitable_result<ReceiveBlockStatus<Cont> > _promise;
    std::shared_ptr<IStream> _stream;

    struct CB {
        ReceiveBlockState *owner;
        void operator()(awaitable<std::string_view> &awt) {
            owner->process_data(awt);
        }
    };

    char _callback_buffer[sizeof(awaiting_callback<std::string_view, CB>)];

    prepared_coro process_data(awaitable<std::string_view> &awt) {
        try {
            std::string_view data = awt.await_resume();
            if (data.empty()) {
                return _promise(false);
            }
            std::size_t remain = _maxbuff - _buff.size();
            if (remain <= data.size()) {
                auto a = data.substr(0,remain);
                auto b = data.substr(remain);
                std::copy(a.begin(),a.end(), std::back_inserter(_buff));
                _stream->put_back(b);
                return _promise(true);
            }
            std::copy(data.begin(), data.end(), std::back_inserter(_buff));
            _stream->receive().set_callback(CB{this}, _callback_buffer);
            return {};
        } catch (...) {
            return _promise.set_exception(std::current_exception());
        }
    }
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
    awaitable<std::string_view> receive() {
        return _ptr->receive();
    }
    void put_back(std::string_view s){
        _ptr->put_back(s);
    }
    bool is_eof() const{
        return _ptr->is_eof();
    }
    awaitable<bool> send(std::string_view data) {
        return _ptr->send(data);
    }
    void send_eof() {
        _ptr->send_eof();
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
    awaitable<ReceiveUntilStatus<Cont, n> > receive_until(Cont &buffer, pattern_search<char, n> patt, size_t limit = ~static_cast<std::size_t>(0)) {
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
        return ReceiveUntilState<Cont, n>(buffer, patt, state, limit, _ptr);

    }

    template<typename Cont, unsigned int n>
    awaitable<ReceiveUntilStatus<Cont, n> > receive_until(Cont &buffer, const char (&sep)[n], size_t limit = ~static_cast<std::size_t>(0)) {        ;
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
        return ReceiveBlockState<Cont>(buffer, size, _ptr);
    }


protected:
    std::shared_ptr<IStream> _ptr;
};

}
