/*
 * stream.h
 *
 *  Created on: 25. 3. 2023
 *      Author: ondra
 */

#ifndef SRC_COROSERVER_STREAM_H_
#define SRC_COROSERVER_STREAM_H_

#include "peername.h"
#include "substring.h"

#include <cocls/future.h>
#include <cocls/with_allocator.h>
#include <chrono>

namespace coroserver {

struct TimeoutSettings {
    unsigned int read_timeout_ms = -1;
    unsigned int write_timeout_ms = -1;

    static std::chrono::system_clock::time_point from_duration(unsigned int dur) {
        if (dur == static_cast<unsigned int>(-1)) return std::chrono::system_clock::time_point::max();
        else return std::chrono::system_clock::now()+std::chrono::milliseconds(dur);
    }
};

class IStream {
public:


    virtual ~IStream() = default;
    virtual cocls::future<std::string_view> read() = 0;
    virtual std::string_view read_nb() = 0;
    virtual void put_back(std::string_view buff) = 0;
    virtual bool is_read_timeout() const = 0;

    virtual cocls::future<bool> write(std::string_view buffer) = 0;
    virtual cocls::future<bool> write_eof() = 0;

    virtual void set_timeouts(const TimeoutSettings &tm) = 0;
    virtual TimeoutSettings get_timeouts() = 0;


    virtual PeerName get_source() const = 0;
    virtual void shutdown() = 0;

    IStream ()= default;
    IStream &operator=(const IStream &) = delete;
    IStream(const IStream &) = delete;
};

class AbstractStream: public IStream {
public:
    virtual void put_back(std::string_view buff) {
        _putback_buffer = buff;
    }

protected:

    std::string_view read_putback_buffer() {
        return std::exchange(_putback_buffer,std::string_view());
    }

    std::string_view _putback_buffer;
};

class AbstractStreamWithMetadata: public AbstractStream {
public:
    AbstractStreamWithMetadata(PeerName &&source, TimeoutSettings &&tms)
        :_source(std::move(source)),_tms(std::move(tms)) {}
    virtual void set_timeouts(const TimeoutSettings &tm) {
        _tms = tm;
    }
    virtual TimeoutSettings get_timeouts() {
        return _tms;
    }
    virtual PeerName get_source() const {
        return _source;
    }
protected:
    PeerName _source;
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
    virtual PeerName get_source() const override {
        return _proxied->get_source();
    }
    virtual bool is_read_timeout() const override {
        return _proxied->is_read_timeout();
    }
    virtual void shutdown() override {
        _proxied->shutdown();
    }

protected:
    std::shared_ptr<IStream> _proxied;

};


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
    cocls::future<std::string_view> read() {return _stream->read();}
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
    cocls::future<bool> write(std::string_view buffer) {return _stream->write(buffer);}
    ///Writes eof and closes the stream
    /**
     * @retval true stream closed
     * @retval false stream has been already closed or timeouted, so closing
     * the stream is impossible
     */
    cocls::future<bool> write_eof() {return _stream->write_eof();}

    void set_timeouts(const TimeoutSettings &tm)  {return _stream->set_timeouts(tm);}
    TimeoutSettings get_timeouts()  {return _stream->get_timeouts();}
    PeerName get_source() const {return _stream->get_source();}
    void shutdown() {return _stream->shutdown();}


    std::shared_ptr<IStream> getStreamDevice() const {
        return _stream;
    }

    ///read until separator reached
    /**
     * The separator is extracted but not stored
     * @param a allocator (storage) to allocate coroutine frame
     * @param container container where to put result
     * @param sep separator to find
     * @param limit specified limit when read is stopped with error. This limit
     * is not hard limit, the container can still receive more items than limit. The
     * primary purpose of this limit is to stop reading when unexpectedly long
     * sentence without separator is read, which can be result of some kind of
     * DoS attack.
     * @retval true success
     * @retval false error or eof or timeout
     */
    template<typename Alloc, typename Container>
    cocls::future<bool> read_until(Alloc &a, Container &container, std::string_view sep, std::size_t limit = std::numeric_limits<std::size_t>::max()) {
        container.clear();
        return read_until_coro(a, *_stream,container, sep, limit);
    }

    ///read until separator reached
    /**
     * The separator is extracted but not stored
     * @param container container where to put result
     * @param sep separator to find
     * @param limit specified limit when read is stopped with error. This limit
     * is not hard limit, the container can still receive more items than limit. The
     * primary purpose of this limit is to stop reading when unexpectedly long
     * sentence without separator is read, which can be result of some kind of
     * DoS attack.
     * @retval true success
     * @retval false error or eof or timeout
     */
    template<typename Container>
    cocls::future<bool> read_until(Container &container, std::string_view sep, std::size_t limit = std::numeric_limits<std::size_t>::max()) {
        container.clear();
        cocls::default_storage stor;
        return read_until_coro(stor, *_stream,container, sep, limit);
    }


protected:

    template<typename Alloc, typename Container>
    static cocls::with_allocator<Alloc, cocls::async<bool> > read_until_coro(Alloc &, IStream &stream, Container &container, std::string_view sep, std::size_t limit) {
        //remember container size - this allows to implement "append" version of this function
        auto csz = container.size();
        //use Knuth-Pratt-Morris search algorithm. Install the object for it
        SubstringSearch<char> srch(sep);
        //read first fragment
        std::string_view buff = co_await stream.read();
        //if the fragment is empty, return failure (eof or timeout)
        if (buff.empty()) co_return false;
        //append the buffer for search, repeat until something is found
        while (!srch.append(buff)) {
            //if container reached limit, failure now
            if (container.size() > limit) co_return false;
            //copy buffer to container
            std::copy(buff.begin(), buff.end(), std::back_inserter(container));
            //read next fragment
            buff = co_await stream.read();
            //if buffer is empty (eof or timeout), report failure
            if (buff.empty()) co_return false;
            //repeat
        }
        //retrieve index of next character
        auto idx = srch.get_pos_after_pattern();
        //calculate remain buffer
        auto remain = buff.substr(idx);
        //retrieve matching fragment
        auto rest = buff.substr(0, idx);
        //copy buffer
        std::copy(rest.begin(), rest.end(), std::back_inserter(container));
        //adjust size
        container.resize(csz + srch.get_global_pos());
        //return back unprocessed data
        stream.put_back(remain);
        //success
        co_return true;
    }


    std::shared_ptr<IStream> _stream;
};

///Stream object, which supports just reading, not writing
class ReadStream {
public:

    ReadStream(Stream stream):_stream(stream.getStreamDevice()) {}
    ReadStream(ReadStream &&stream):_stream(std::move(stream._stream)) {}
    ReadStream(const ReadStream &) = delete;
    ReadStream &operator=(const ReadStream &) = delete;

    cocls::future<std::string_view> read() {return _stream->read();}
    std::string_view read_nb() {return _stream->read_nb();}
    void put_back(std::string_view buff) {return _stream->put_back(buff);}
    virtual bool is_read_timeout() const {return _stream->is_read_timeout();}

    template<typename Alloc, typename Container>
    cocls::future<bool> read_until(Alloc &a, Container &container, std::string_view sep, std::size_t limit = std::numeric_limits<std::size_t>::max()) {
        return Stream::read_until_coro(a, *_stream, container, sep, limit);
    }

    template<typename Container>
    cocls::future<bool> read_until(Container &container, std::string_view sep, std::size_t limit = std::numeric_limits<std::size_t>::max()) {
        cocls::default_storage stor;
        return Stream::read_until_coro(stor, *_stream, container, sep, limit);
    }


protected:
    std::shared_ptr<IStream> _stream;

};

///Stream object, which supports just writing not reading
class WriteStream {
public:

    WriteStream(Stream stream): _stream(stream.getStreamDevice()) {}

    cocls::future<bool> write(std::string_view buffer) {return _stream->write(buffer);}
    cocls::future<bool> write_eof() {return _stream->write_eof();}
    void shutdown() {return _stream->shutdown();}

protected:
    std::shared_ptr<IStream> _stream;

};

}




#endif /* SRC_COROSERVER_STREAM_H_ */
