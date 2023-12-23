/*
 * stream.h
 *
 *  Created on: 25. 3. 2023
 *      Author: ondra
 */

#ifndef SRC_COROSERVER_STREAM_H_
#define SRC_COROSERVER_STREAM_H_

#include "peername.h"
#include "strutils.h"
#include "timeout.h"

#include <coro.h>
#include <chrono>


namespace coroserver {
class Stream;

class IStream {
public:


    struct Counters {
        std::size_t read = 0;
        std::size_t write = 0;
    };

    virtual ~IStream() = default;
    virtual coro::future<std::string_view> read() = 0;
    virtual std::string_view read_nb() = 0;
    virtual void put_back(std::string_view buff) = 0;
    virtual bool is_read_timeout() const = 0;

    virtual coro::future<bool> write(std::string_view buffer) = 0;
    virtual coro::future<bool> write_eof() = 0;

    virtual void set_timeouts(const TimeoutSettings &tm) = 0;
    virtual TimeoutSettings get_timeouts() = 0;

    virtual Counters get_counters() const noexcept = 0;

    virtual PeerName get_peer_name() const = 0;

    virtual void shutdown() = 0;

    IStream ()= default;
    IStream &operator=(const IStream &) = delete;
    IStream(const IStream &) = delete;
};

class AbstractStream: public IStream {
public:
    virtual void put_back(std::string_view buff) override {
        _putback_buffer = buff;
    }
    virtual std::string_view read_nb() override {
        return read_putback_buffer();
    }

protected:

    std::string_view read_putback_buffer() {
        return std::exchange(_putback_buffer,std::string_view());
    }

    std::string_view _putback_buffer;
};

class AbstractStreamWithMetadata: public AbstractStream {
public:
    AbstractStreamWithMetadata(TimeoutSettings &&tms)
        :_tms(std::move(tms)) {}
    virtual void set_timeouts(const TimeoutSettings &tm) override{
        _tms = tm;
    }
    virtual TimeoutSettings get_timeouts() override{
        return _tms;
    }
protected:
    TimeoutSettings _tms;
};

class AbstractProxyStream: public AbstractStream {
public:
    AbstractProxyStream(std::shared_ptr<IStream> proxied):_proxied(std::move(proxied)) {}
    virtual void set_timeouts(const TimeoutSettings &tm) override {
        _proxied->set_timeouts(tm);
    }
    virtual TimeoutSettings get_timeouts() override {
        return _proxied->get_timeouts();
    }
    virtual bool is_read_timeout() const override {
        return _proxied->is_read_timeout();
    }
    virtual Counters get_counters() const noexcept override  {
        return _proxied->get_counters();
    }
    virtual void shutdown() override {
        return _proxied->shutdown();
    }
    virtual PeerName get_peer_name() const override {
        return _proxied->get_peer_name();
    }

protected:
    std::shared_ptr<IStream> _proxied;

};

class ReadUntilFuture;

///Generic stream
/**
 * @note MT Safety - stream is MT Unsafe with exception. It is safe to
 * read the stream from one thread and write to the stream in other thread.
 * As the operations can be called from coroutines, the same logic applies here.
 * There should be only one reader and one writer. The reader and writer, both
 * are allowed to be parallel. But run other operations in parallel is UB.
 *
 * For example, you have to avoid to call read(), read_nb() and put_back()
 * in parallel
 * You have to avoid to call write() and write_eof() in parallel. Also note
 * that function setting the timeout cannot be used to interrupt current pending
 * operation.
 *
 * Only function which can be called from different thread regardless on stream state
 * is shutdown(), which allows to unblock any pending operation to fast close the stream
 */
class Stream {
public:
    Stream() = default;

    Stream(std::shared_ptr<IStream> s):_stream(std::move(s)) {}

    ///Read the stream, asynchronously
    /**
     * @return future which is eventually filled with read data. It can be
     * also filled with empty string, which means eof or timeout. For simple
     * usage, you can treat this as eof, as timeout means, that other side
     * is not able to communicate. However if you need to distinguish between
     * eof and timeout, you can use is_read_timeout() function, which returns
     * true for this case.
     *
     * When empty string is returned because eof, subsequent reads immediatelly
     * returns empty string. In case of timeout, subsequent read continues
     * in reading and can block for another timeout period
     *
     */
    coro::future<std::string_view> read() {return _stream->read();}
    ///Reads non-blocking mode
    /**
     * checks for stream state, and if is there any unprocessed data, they
     * are immediatelly returned, otherwise it returns empty string. No blocking
     * is involved. You cannot detect eof/timeout by this function
     * @return string
     */
    std::string_view read_nb() {return _stream->read_nb();}
    ///put back part of unprocessed data, they can be retrieved by next read() or read_nb()
    /**
     * @param buff buffer to put back
     *
     * @note You can call this function only once between reads(). Subsequent
     * calls replaces buffer. Also note that buffer is passed by reference. It
     * is OK, if buffer is part of buffer returned by read(), but if you pass
     * a different buffer, you need to ensure, that data in the buffer stays valid
     * until they are read
     */
    void put_back(std::string_view buff) {return _stream->put_back(buff);}
    ///Determines last state of the read.
    /**
     * @retval true last read was unsuccessful, because timeout. Repeat reading
     * operation extends timeout
     * @retval false last read was not timeout. If the last read was empty string,
     * then it was EOF. Function returns false, if the last read
     * returned a nonzero-length string
     */
    bool is_read_timeout() const {return _stream->is_read_timeout();}

    ///writes the buffer
    /**
     * @param buffer buffer to write
     * @return a future which is eventually resolved with status of operation.
     * @retval true write operation successed
     * @retval false write operation failed, connestion closed, or timeouted. Note
     * that there is no way to restart timeouted write, as the not written data
     * are lost. Once the stream is in this state, subsequent calls returns false
     * immediately
     *
     */
    coro::future<bool> write(std::string_view buffer) {return _stream->write(buffer);}
    ///Writes eof and closes the stream
    /**
     * @retval true stream closed
     * @retval false stream has been already closed or timeouted, so closing
     * the stream is impossible
     */
    coro::future<bool> write_eof() {return _stream->write_eof();}

    void set_timeouts(const TimeoutSettings &tm)  {return _stream->set_timeouts(tm);}
    TimeoutSettings get_timeouts()  {return _stream->get_timeouts();}
    PeerName get_peer_name() const {return _stream->get_peer_name();}
    void shutdown() {return _stream->shutdown();}

    ///Retrieves io counters
    /**
     * @return object which contains total count of read and write bytes for lifetime
     * of this stream. It allows to measure size and speed of transfer.
     */
    auto get_counters() const {return _stream->get_counters();}

    std::shared_ptr<IStream> getStreamDevice() const {
        return _stream;
    }

    ///creates stream, which doesn't send or receive any data;
    /**
     * @return the stream has no data, and no data can be written
     *
     * Stream object purposely has no default constructor, so you cannot create
     * unitialized stream, unless you specify nullptr as stream object, which
     * visually manifests that stream is purposely uninitialized. Other way
     * how to safely create stream where is no stream available is to use null_stream();
     *
     */
    static Stream null_stream();

    std::shared_ptr<IStream> _stream;
};

}

#if 0

///Helper class of function read_until()
template<typename Container, typename _SearchKMP>
class ReadUntil {
public:
    ReadUntil(std::shared_ptr<IStream> &s, Container &container, _SearchKMP kmp, std::size_t limit)
        :_s(s)
        ,_container(container)
        ,_kmp(kmp)
        ,_limit(limit)
    {
        coro::target_member_fn_activation<&ReadUntil::data_read>(_target, this);
    }

    ReadUntil(const ReadUntil &) = delete;
    ReadUntil &operator=(const ReadUntil &) = delete;

    operator bool() {
        return run().get();
    }
    auto operator co_await() {
        return run();
    }

protected:
    coro::future<std::string_view> _fut;
    coro::future<std::string_view>::target_type _target;
    std::shared_ptr<IStream> _s;
    Container &_container;
    _SearchKMP _kmp;
    std::size_t _limit;
    coro::promise<bool> _promise;
    unsigned int _state = 0;

    coro::awaiter<coro::future<bool> > run() {
        return [&](auto promise) {
            _state = 0;
            _promise = std::move(promise);
            _fut << [&]{return _s->read();};
            _fut.register_target(_target);

        };
    }

    void data_read(coro::future<std::string_view> *f) noexcept {
        try {
            std::string_view data = *f;
            if (data.empty()) {
                _promise(false);
                return ;

            }
            for (std::size_t i = 0; i < data.size(); i++) {
                _container.push_back(data[i]);
                if (_kmp(_state, data[i])) {
                    _s->put_back(data.substr(i+1));
                    _promise(true);
                    return;
                }
            }
            if (_container.size() >= _limit) {
                _promise(false);
                return;
            }
            _fut << [&]{return _s->read();};
            _fut.register_target(_target);
        } catch (...) {
            _promise(std::current_exception());
        }

    }



};

template<typename Container>
class ReadBlock {
public:
    ReadBlock(std::shared_ptr<IStream> &s, Container &container, std::size_t limit)
        :_s(s)
        ,_container(container)
        ,_limit(limit)
    {
        coro::target_member_fn_activation<&ReadUntil::data_read>(_target, this);
    }
    ReadBlock(const ReadBlock &) = delete;
    ReadBlock &operator=(const ReadBlock &) = delete;

    operator bool() {
        return run().get();
    }
    auto operator co_await() {
        return run();
    }


protected:
    std::shared_ptr<IStream> _s;
    Container &_container;
    std::size_t _limit;
    coro::future<std::string_view> _fut;
    coro::future<std::string_view>::target_type _target;
    coro::promise<bool> _promise;

    coro::awaiter<coro::future<bool> > operator()() {
        return [&](auto promise) {
            _promise = std::move(promise);
            _awt << [&]{return _s->read();};
        };
    }


    coro::suspend_point<void> data_read(coro::future<std::string_view> &f) noexcept {
        try {
            std::string_view data = *f;
            if (data.empty()) return _promise(false);
            for (std::size_t i = 0; i < data.size(); i++) {
                _container.push_back(data[i]);
                if (_container.size() == _limit) {
                    _s->put_back(data.substr(i+1));
                    return _promise(true);
                }
            }
            _awt << [&]{return _s->read();};
            return {};
        } catch (...) {
            return _promise(std::current_exception());
        }
    }

};

class DiscardBlock {
public:
    DiscardBlock(std::shared_ptr<IStream> &s, std::size_t limit)
        :_s(s)
        ,_limit(limit)
        ,_awt(*this) {}
    DiscardBlock(const DiscardBlock &) = delete;
    DiscardBlock &operator=(const DiscardBlock &) = delete;

    coro::future<bool> operator()() {
        return [&](auto promise) {
            _promise = std::move(promise);
            _awt << [&]{return _s->read();};
        };
    }
    coro::future_awaiter<bool> operator co_await() {
        return [&]{return (*this)();};
    }

protected:
    coro::suspend_point<void> data_read(coro::future<std::string_view> &f) noexcept {
        try {
            std::string_view data = *f;
            if (data.empty()) return _promise(false);
            if (data.size() < _limit) {
                _s->put_back(data.substr(_limit));
                return _promise(true);
            } else {
                _limit -= data.size();
            }
            _awt << [&]{return _s->read();};
            return {};
        } catch (...) {
            return _promise(std::current_exception());
        }
    }

    std::shared_ptr<IStream> &_s;
    std::size_t _limit;
    coro::call_fn_future_awaiter<&DiscardBlock::data_read> _awt;
    coro::promise<bool> _promise;
};



///Creates generator-like object, which reads data until a separator is found
/**
 * @param container reference to container, where data will be stored. You need to
 * keep container valid during generation
 * @param pattern pattern to search, it is declared as search_kmp object. It
 * is expected, that separators constaints
 * @param limit bytes to read until error is reported. This is soft limit for
 * size of container. If the container reaches the size, the reading stops and
 * error is reported
 *
 * @return ReadUntil object which can be directly co_awaited or called to
 * retrieve a future with result
 *
 * @note separator is included and stored to the container
 *
 * @note it is generator-like object, it doesn't allocate memory.
 *
 * @code
 *
 */
template<typename Container, unsigned int N>
auto read_until(Container &container, const search_kmp<N> &pattern, std::size_t limit = std::numeric_limits<std::size_t>::max()) {
    container.clear();
    return ReadUntil<Container, const search_kmp<N> &>(_stream, container, pattern, limit);
}

///Creates generator-like object, which reads data until a separator is found
/**
 * @param container reference to container, where data will be stored. You need to
 * keep container valid during generation
 * @param pattern pattern to search, it is declared as std::string_view object. If the
 * string is constant, consider to use search_kmp<> type instead
 * @param limit bytes to read until error is reported. This is soft limit for
 * size of container. If the container reaches the size, the reading stops and
 * error is reported
 *
 * @return generator object. It is callable object, which can be called without
 * arguments. Result of the call is future<bool>. The value of call is true -
 * successfully read until separator, or false - reached end of stream
 * @retval true read success
 * @retval false failure - eof, timeout or limit
 *
 * @note separator is included to container
 *
 * @note it is generator-like object, it doesn't allocate memory.
 *
 * @code
 *
 */
template<typename Container, unsigned int N>
auto read_until(Container &container, std::string_view pattern, std::size_t limit = std::numeric_limits<std::size_t>::max()) {
    return ReadUntil<Container, search_kmp<0> >(_stream, container, pattern, limit);
}

///Read specified count of bytes
/**
 * @param container container object where result will be stored
 * @param size count of bytes
 * @return Object which controls whole operation and which can be directly
 * co_awaited for the result
 * @retval true read successfully
 * @retval false eof or timeout reached
 */
template<typename Container>
ReadBlock<Container> read_block(Container &container, std::size_t size) {
    container.clear();
    return ReadBlock<Container>(_stream, container, size);
}


///Discard any input up to specified count of bytes
/**
 * @param count count of bytes to discard. Default value discards all bytes
 * until EOF. If you specify 0, no bytes will be discarded, however an empty
 * read is still performed and return value is set appropriately
 * @retval true all bytes has been discarded, EOF not reached. In case that count
 * is zero, it manifests, that there are still data in the stream
 * @retval false not all bytes has been discarded, EOF has been reached. If case
 * that count is zero, this means no more data are available.
 */
DiscardBlock dicard(std::size_t count = -1) {
    return DiscardBlock(_stream, count);
}

///Write generated data
/**
 * @param gen generator which generates strings. This must be finite generator
 * as the writing stops when generator is done
 * @return Object which handles state of the writing and generating. This
 * object is directly awaitable (so you can co_await it). To use
 * outside of coroutine or if you want to obtain the future, you need to
 * use operator() of this object.
 * @retval true success
 * @retval false connection reset during write
 */
GenerateAndWrite generate_and_write(coro::generator<std::string_view> &&gen) {
    return GenerateAndWrite(_stream, std::move(gen));
}

#endif


#endif /* SRC_COROSERVER_STREAM_H_ */
