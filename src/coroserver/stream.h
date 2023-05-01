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

#include <cocls/future.h>
#include <cocls/async.h>
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


    struct Counters {
        std::size_t read = 0;
        std::size_t write = 0;
    };

    virtual ~IStream() = default;
    virtual cocls::future<std::string_view> read() = 0;
    virtual std::string_view read_nb() = 0;
    virtual void put_back(std::string_view buff) = 0;
    virtual bool is_read_timeout() const = 0;

    virtual cocls::future<bool> write(std::string_view buffer) = 0;
    virtual cocls::future<bool> write_eof() = 0;

    virtual void set_timeouts(const TimeoutSettings &tm) = 0;
    virtual TimeoutSettings get_timeouts() = 0;

    virtual Counters get_counters() const noexcept = 0;

    virtual PeerName get_peer_name() const = 0;

    virtual PeerName get_interface_name() const = 0;

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
        _proxied->shutdown();
    }
    virtual PeerName get_peer_name() const override {
        return _proxied->get_peer_name();
    }
    virtual PeerName get_interface_name() const {
        return _proxied->get_interface_name();
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
    PeerName get_peer_name() const {return _stream->get_peer_name();}
    PeerName get_interface_name() const {return _stream->get_interface_name();}
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

    ///read until separator reached
    /**
     * The separator is also extracted and stored.
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
     * The separator is also extracted and stored.
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


    ///Read specified count of bytes
    /**
     * @param a allocator for coroutine frame
     * @param container container object where result will be stored
     * @param size count of bytes
     * @retval true read successfully
     * @retval false eof or timeout reached
     */
    template<typename Alloc, typename Container>
    cocls::future<bool> read_block(Alloc &a, Container &container, std::size_t size) {
        container.clear();
        return read_block_coro(a, *_stream, container, size);
    }


    ///Read specified count of bytes
    /**
     * @param container container object where result will be stored
     * @param size count of bytes
     * @retval true read successfully
     * @retval false eof or timeout reached
     */
    template<typename Container>
    cocls::future<bool> read_block(Container &container, std::size_t size) {
        container.clear();
        cocls::default_storage stor;
        return read_block_coro(stor, *_stream, container, size);
    }

    ///Discard any input up to specified count of bytes
    /**
     * @param Alloc coroutine allocator (cocls)
     * @param count count of bytes to discard. Default value discards all bytes
     * until EOF. If you specify 0, no bytes will be discarded, however an empty
     * read is still performed and return value is set apropriately
     * @retval true all bytes has been discarded, EOF not reached. In case that count
     * is zero, it manifests, that there are still data in the stream
     * @retval false not all bytes has been discarded, EOF has been reached. If case
     * that count is zero, this means no more data are available.
     */
    template<typename Alloc>
    cocls::future<bool> discard(Alloc &a, std::size_t count = -1) {
        return discard_coro(a, *_stream, count);
    }

    ///Discard any input up to specified count of bytes
    /**
     * @param count count of bytes to discard. Default value discards all bytes
     * until EOF. If you specify 0, no bytes will be discarded, however an empty
     * read is still performed and return value is set apropriately
     * @retval true all bytes has been discarded, EOF not reached. In case that count
     * is zero, it manifests, that there are still data in the stream
     * @retval false not all bytes has been discarded, EOF has been reached. If case
     * that count is zero, this means no more data are available.
     */
    cocls::future<bool> discard(std::size_t count = -1) {
        cocls::default_storage stor;
        return discard_coro(stor, *_stream, count);
    }





protected:

    template<typename Alloc, typename Container>
    static cocls::with_allocator<Alloc, cocls::async<bool> > read_until_coro(Alloc &, IStream &stream, Container &container, std::string_view sep, std::size_t limit) {
        //separator must be non-empty, otherwise empty container is result
        if (sep.empty()) co_return true;
        //initialize KMP
        auto srch = search_pattern(sep);
        //read first fragment
        std::string_view buff = co_await stream.read();
        //while buffer is not empty
        std::size_t cnt = 0;
        while (!buff.empty()) {
            cnt+=buff.size();
            //add extra section to define variables used outside of co_await section
            {
                //offset counter
                std::size_t ofs = 0;
                //process each character
                for (auto c: buff) {
                    ++ofs;
                    //put to bufer
                    container.push_back(c);
                    //use KMP to match pattern, if true returns, c completed the pattern
                    if (srch(c)) {
                        //putback rest of buffer
                        stream.put_back(buff.substr(ofs));
                        //return success
                        co_return true;
                    }
                }
            }
            //processed all characters, read next
            buff = co_await stream.read();
            if (limit < cnt) co_return false;
        }
        co_return false;
    }

    template<typename Alloc, typename Container>
    static cocls::with_allocator<Alloc, cocls::async<bool> > read_block_coro(Alloc &, IStream &stream, Container &container, std::size_t size) {
        while (size > 0) {
            std::string_view buff = co_await stream.read();
            if (buff.empty()) co_return false;
            std::string_view p = buff.substr(0, size);
            std::string_view q = buff.substr(p.size());
            stream.put_back(q);
            size -= p.size();
            std::copy(p.begin(), p.end(), std::back_inserter(container));
        }
        co_return true;
    }

    template<typename Alloc>
    static cocls::with_allocator<Alloc, cocls::async<bool> > discard_coro(Alloc &, IStream &stream, std::size_t sz) {
        if (sz == 0) {
            std::string_view b = co_await stream.read();
            stream.put_back(b);
            co_return !b.empty();
        }
        while (sz) {
            std::string_view b = co_await stream.read();
            if (b.empty()) co_return false;
            if (sz < b.size()) {
                stream.put_back(b.substr(sz));
                sz = 0;
            } else {
                sz -= b.size();
            }
        }
        co_return true;
    }



    std::shared_ptr<IStream> _stream;
};


}




#endif /* SRC_COROSERVER_STREAM_H_ */
