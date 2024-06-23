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

#include "coro_common.h"
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


using BinBuffer = std::vector<char>;

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


    template<typename KMP>
    class ReadUntil {
    public:

        ReadUntil( std::shared_ptr<IStream> &s, KMP pattern, std::size_t limit, BinBuffer &buffer)
            :_s(s),_pattern(pattern),_limit(limit),_buffer(buffer) {}
        coro::future<std::string_view> initiate() {
            return [&](auto promise) {
                _buffer.clear();
                _srch = _pattern;
                _prom = std::move(promise);
                _rdr << [this]{return _s->read();};
                _rdr >> [this]{process();};
            };
        }

    protected:
        std::shared_ptr<IStream> &_s;
        KMP _pattern;
        kmp_search<char> _srch;
        std::size_t _limit;
        BinBuffer &_buffer;

        coro::future<std::string_view> _rdr;
        coro::promise<std::string_view> _prom;

        auto process() {
            try {
                std::string_view data = _rdr;
                if (data.empty()) {
                    if (!_buffer.empty()) {
                        return _prom(_buffer.data(), _buffer.size());
                    } else {
                        return _prom.cancel();
                    }
                }
                std::size_t sz = data.size();
                for (std::size_t i = 0; i < sz; ++i) {
                    if (_srch(data[i])) {
                        std::size_t e = i + 1;
                        _s->put_back(data.substr(e));
                        if (_buffer.empty()) {
                            return _prom(data.data(), e - _srch.size());
                        } else {
                            _buffer.insert(_buffer.end(), data.data(), data.data()+e);
                            return _prom(_buffer.data(), _buffer.size()- _srch.size());
                        }
                    }
                }
                _buffer.insert(_buffer.end(), data.begin(), data.end());
                if (_buffer.size() > _limit) return _prom.cancel();
                _rdr << [this]{return _s->read();};
                _rdr >> [this]{process();};
                return coro::promise<std::string_view>::notify();
            } catch (...) {
                return _prom.reject();
            }
        }

    };


    ///Read until specified sequence is found
    /**
     * @param buffer temporary buffer, must be declared by caller and it is used to
     * store temporary data. The caller can preallocate buffer or reuse buffer when
     * the function is called repeatedly
     *
     * @param srch search pattern
     * @param limit limit. This is security limit to avoid processing too long sequences
     * of data. If the limit is reached, the processing is canceled like end of stream. Note
     * that limit is not checked for exact value, so it is still possible to receive
     * longer sequence then specified limit. Default value is unlimited
     *
     * @return awaitable object, which returns string_view containing the sequence excluding
     * the pattern. If the pattern is not reached until end of stream, the remainig
     * data are returned. If no data are extracted, cancels await operation
     */
    template<unsigned int N>
    awaitable<std::string_view, ReadUntil<kmp_pattern<char, N> > >read_until(BinBuffer &buffer, kmp_pattern<char, N> srch, std::size_t limit = std::numeric_limits<std::size_t>::max()) {
        return {_stream, srch, limit, buffer};
    }
    ///Read until specified sequence is found
    /**
     * @param buffer temporary buffer, must be declared by caller and it is used to
     * store temporary data. The caller can preallocate buffer or reuse buffer when
     * the function is called repeatedly
     *
     * @param srch search pattern
     * @param limit limit. This is security limit to avoid processing too long sequences
     * of data. If the limit is reached, the processing is canceled like end of stream. Note
     * that limit is not checked for exact value, so it is still possible to receive
     * longer sequence then specified limit. Default value is unlimited
     *
     * @return awaitable object, which returns string_view containing the sequence excluding
     * the pattern. If the pattern is not reached until end of stream, the remainig
     * data are returned. If no data are extracted, cancels await operation
     */
    awaitable<std::string_view, ReadUntil<kmp_pattern<char, 0> > >read_until(BinBuffer &buffer, std::string_view srch, std::size_t limit = std::numeric_limits<std::size_t>::max()) {
        return {_stream, srch, limit, buffer};
    }


    class BlockReadAwaitable: public coro::future<std::string_view> {
    public:

        BlockReadAwaitable( std::shared_ptr<IStream> &s, std::size_t limit, BinBuffer &buffer)
            :_stream(s),_limit(limit),_buffer(buffer) {

            _buffer.clear();
            _prom = this->get_promise();
            _rdr << [this]{return _stream->read();};
            _rdr >> [this]{process();};
        }

    protected:
        std::shared_ptr<IStream> &_stream;
        std::size_t _limit;
        BinBuffer &_buffer;

        coro::future<std::string_view> _rdr;
        coro::promise<std::string_view> _prom;

        coro::promise<std::string_view>::notify process() {
            try {
                std::string_view data = _rdr;
                if (data.empty()) {
                    return _prom(_buffer.data(), _buffer.size());
                }
                auto remain = _limit - _buffer.size();
                if (remain <= data.size()) {
                    _stream->put_back(data.substr(remain));
                    data = data.substr(0,remain);
                    _buffer.insert(_buffer.end(), data.begin(), data.end());
                    return _prom(_buffer.data(), _buffer.size());
                }
                _buffer.insert(_buffer.end(), data.begin(), data.end());
                _rdr << [this]{return _stream->read();};
                _rdr >> [this]{process();};
                return {};
            } catch (...) {
                return _prom.reject();
            }
        }
    };

    BlockReadAwaitable block_read(BinBuffer &buffer, std::size_t limit) {
        return {_stream, limit, buffer};
    }

protected:
    std::shared_ptr<IStream> _stream;
};

}




#endif /* SRC_COROSERVER_STREAM_H_ */
