#pragma once
#ifndef _SRC_COROSERVER_MT_STREAM_qoiwujdoiqjdoiqdjoq_
#define _SRC_COROSERVER_MT_STREAM_qoiwujdoiqjdoiqdjoq_
#include "stream.h"

#include <cocls/mutex.h>
#include <cocls/coro_storage.h>
#include <vector>
#include <memory>
#include <concepts>
#include <optional>



namespace coroserver {

///This class helps with multithreaded writing to a stream
/**
 * The class contains a buffer. When multiple threads is trying
 * to write, the each thread appends to a buffer, which is
 * eventually written to the stream. Writing to this
 * object is not asynchronous operation, so the writer don't
 * need to be a coroutine. You can also pass a serializing function, which
 * handles writing to an internal buffer while the stream is locked
 *
 */
class MTStreamWriter {
public:

    ///Construct the object
    /**
     * @param device stream device
     */
    MTStreamWriter(std::shared_ptr<IStream> device)
        :stream(std::move(device))
        ,_awt(*this) {}

    ///Construct the object
    /**
     * @param s stream
     */
    MTStreamWriter(Stream s):MTStreamWriter(s.getStreamDevice()) {}

    ///Write using a function
    /**
     * @param fn a function, possible lambda function with one argument
     * (possible auto argument), the argument is function, which accepts
     * a character to write into a buffer
     * @return suspend point which carries boolean flag. Suspend point
     * can be used for co_await
     *
     * @retval true data written to buffer
     * @retval false write is impossible
     * @exception any any exception captured during recent flush
     */
    template<typename Fn>
    CXX20_REQUIRES(std::invocable<Fn, decltype([](char){})>)
    cocls::suspend_point<bool> operator()(Fn &&fn) {
        std::unique_lock lk(_mx);
        if (_e) std::rethrow_exception(_e);
        if (_closed) return false;
        fn([&](char c){_prepared.push_back(c);});
        if (_pending) return true;
        _pending = true;
        std::swap(_prepared,_pending_write);
        auto out = cocls::suspend_point<bool>(on_flush(),true);
        lk.unlock();
        _awt << [&]{return stream->write({_pending_write.data(),_pending_write.size()});};
        return out;
    }


    ///write prepared message
    /**
     * @param obj data to write
     * @return suspend point which carries boolean flag. Suspend point
     * can be used for co_await
     *
     * @retval true data written to buffer
     * @retval false write is impossible
     * @exception any any exception captured during recent flush
     */
    cocls::suspend_point<bool> operator()(std::string_view txt) {
        return (*this)([txt](auto wr){
            for (auto y:txt) wr(y);
        });
    }


    ///Returns true, if writing is possible
    operator bool () const {
        std::lock_guard _(_mx);
        if (_e) std::rethrow_exception(_e);
        return !_closed;
    }

    ///Retrieves total size in bytes in buffer waiting to be send
    /**
     * @return total size of all currently active buffers represents amount
     * of pending bytes
     * @exception any any exception captured during recent flush
     *
     */
    std::size_t get_buffered_size() const {
        std::lock_guard _(_mx);
        return _prepared.size() + _pending_write.size();
    }

    ///Creates a synchronization future, which becomes resolved, when the object is idle
    /**
     * Writing to the stream is done at background. This function helps to synchronize
     * with idle state of the object, where there is no pending write operation.
     *
     * You can use this function before the object is destroyed to ensure, that nothing
     * is using this object. However you also need to ensure, that there is no
     * other thread with pushing data to it.
     *
     * @note to speed up operation, you should also shutdown the stream
     *
     * @note Function is not check for close state. It can still resolve the future
     * even if the stream is still open, the only condition is finishing all pending
     * operation
     *
     * @note In compare to wait_for_flush(), this operation doesn't resolve
     * the future, when there is non-empty buffer waiting to be written
     */
    cocls::future<void> wait_for_idle() {
        return [&](auto promise) {
            std::lock_guard _(_mx);
            _waiting.push_back({true, std::move(promise)});
        };
    }

    ///Creates a synchronization future. which becomes resolved, when after a flush operation is performed
    /**
     * You can use this future to slow down writes which can inflate the internal buffer
     * The future allows to wait until the buffer is internally flushed. The flush
     * operation is handled automatically, but it can be delayed when the
     * speed of the network is low.
     * @return future
     *
     * @note In compare to wait_for_idle(), this operation resolves the future
     * after the data are sent, but before the write is complete.
     */
    cocls::future<void> wait_for_flush() {
        return [&](auto promise) {
            std::lock_guard _(_mx);
            _waiting.push_back({false, std::move(promise)});
        };
    }

    ///Close output, the stream will receive closed status
    /** This function doesn't writes anything to the output, it
     * just sets closing state. Any pending and buffered data will
     * be eventually written
     */
    void close() {
        std::lock_guard _(_mx);
        _closed = true;
    }

    ///Write eof to the output stream
    /**
     * You can call this function even if there are pending writes. This
     * request is buffered and eof is written when all pending writes
     * are complete.
     *
     * @retval true buffered
     * @retval false failed, the stream is already closed
     * @note function also closes the stream
     */
    bool write_eof() {
        bool p;
        std::unique_lock lk(_mx);
        if (_closed) return false;
        _closed = true;
        _write_eof = true;
        p = _pending;
        lk.unlock();
        if (!p) _awt << [&]{return stream->write_eof();};
        return true;
    }

    ///Destroyes the object. Ensure, that there is no pending operation
    /**
     * You can destroy the object when there is no pending operation. Use
     * wait_for_idle() to synchronize with this state
     */
    ~MTStreamWriter() {
        assert(!_pending && "Destroying object with pending operation. Use wait_for_idle() to avoid this assert");
    }

    auto getStreamDevice() const {
        return stream;
    }

    ///Create shared version of MTStreamWriter
    /**
     * It is recommended instead of calling std::make_shared, as this
     * also handles correct destruction through the deleter
     *
     * @param s
     * @return
     */
    static std::shared_ptr<MTStreamWriter> make_shared(Stream s) {
        return std::shared_ptr<MTStreamWriter>(new MTStreamWriter(s), Deleter{});
    }

protected:
    cocls::suspend_point<void> finish_write(cocls::future<bool> &val) noexcept {
        std::unique_lock lk(_mx);
        try {
            _pending_write.clear();
            _closed = !*val;
            if (_closed) {
                _prepared.clear();
                _pending = false;
                return on_idle();
            } else if (_prepared.empty()) {
                _pending = false;
                return on_idle();
            } else {
                std::swap(_pending_write,_prepared);
                cocls::suspend_point<void> out = on_flush();
                lk.unlock();
                _awt << [&]{return stream->write({_pending_write.data(),_pending_write.size()});};
                return out;
            }
        } catch (...) {
            _e = std::current_exception();
            _prepared.clear();
            _pending = false;
            _closed = true;
            return on_idle();
        }
    }


    std::shared_ptr<IStream> stream;
    mutable std::mutex _mx;
    std::vector<char> _prepared;
    std::vector<char> _pending_write;
    std::vector<std::pair<bool,cocls::promise<void> > > _waiting;
    cocls::call_fn_future_awaiter<&MTStreamWriter::finish_write> _awt;
    bool _closed = false;
    bool _pending = false;
    bool _write_eof = false;
    std::exception_ptr _e;

    cocls::suspend_point<void> on_idle() {
        //on_idle flushes all waiting promises
        cocls::suspend_point<void> out;
        for (auto &x: _waiting) out << x.second();
        _waiting.clear();
        return out;
    }
    cocls::suspend_point<void> on_flush() {
        //flush only on_flush promises
        cocls::suspend_point<void> out;
        _waiting.erase(std::remove_if(_waiting.begin(), _waiting.end(),[&](auto &p){
            if (p.first) return false;
            out << p.second();
            return true;
        }), _waiting.end());
        return out;
    }

    cocls::suspend_point<void> on_destroy(cocls::future<void> &) noexcept {
        _destroy_awt.~call_fn_future_awaiter();
        delete this;
        return {};
    }
    union {
        cocls::call_fn_future_awaiter<&MTStreamWriter::on_destroy> _destroy_awt;
    };

    void destroy() {
        std::construct_at(&_destroy_awt, *this);
        _destroy_awt << [&]{return this->wait_for_idle();};
    }

    struct Deleter {
        void operator()(MTStreamWriter *inst) {
            inst->destroy();
        }
    };



};

namespace _details {

class MTWriteStreamInstance: public AbstractStream {
public:
    MTWriteStreamInstance(std::shared_ptr<IStream> device)
        :_writer(device),_awt(*this) {}
    virtual cocls::future<bool> write(std::string_view buffer) override {
        return cocls::future<bool>::set_value(_writer(buffer));
    }
    virtual cocls::future<bool> write_eof() override {
        return cocls::future<bool>::set_value(_writer.write_eof());
    }
    virtual TimeoutSettings get_timeouts() override {
        return _writer.getStreamDevice()->get_timeouts();
    }
    virtual cocls::future<std::string_view> read() override {
        return cocls::future<std::string_view>::set_value(read_putback_buffer());
    }
    virtual IStream::Counters get_counters() const noexcept override{
        return IStream::Counters{0,_writer.getStreamDevice()->get_counters().write};
    }
    virtual PeerName get_peer_name() const override {return {};}
    virtual bool is_read_timeout() const override {return false;}
    virtual void set_timeouts(const TimeoutSettings &tm) override {
        _writer.getStreamDevice()->set_timeouts({
            _writer.getStreamDevice()->get_timeouts().read_timeout_ms,
            tm.write_timeout_ms
        });
    }
    virtual cocls::suspend_point<void> shutdown() override {
        return _writer.getStreamDevice()->shutdown();
    }

    void destroy() {
        _awt << [&]{return _writer.wait_for_idle();};
    }

protected:
    MTStreamWriter _writer;
    cocls::suspend_point<void> on_idle(cocls::future<void> &) noexcept {
        delete this;
        return {};
    }
    cocls::call_fn_future_awaiter<&MTWriteStreamInstance::on_idle> _awt;
};

struct MTWriteStreamDeleter {
    void operator()(MTWriteStreamInstance *inst) {
        inst->destroy();
    };
};

}

///Creates output stream from given stream, which is MT Safe for multiple writes
/**
 * Returned stream can be written by multiple threads. On top of it,
 * write functions are always synchronous.
 *
 * @param s
 * @return
 */
Stream createMTSafeOutputStream(Stream s) {
    return Stream(std::shared_ptr<IStream>(new _details::MTWriteStreamInstance(s.getStreamDevice()), _details::MTWriteStreamDeleter{}));
}


///This class helps with multithreaded reading from a stream
/**
 * The class implements a lock with ability to read stream through
 * a process function, which is called repeatedly to collect as much
 * data as it needs. Other thread can also perform this operation,
 * while it the operation will be suspended until the currently pending
 * operation is finished
 */
class ReadFromStreamMT {
public:


    using ProcessResult = std::optional<std::string_view>;
    ///Read and process input
    /**
     * @param s stream to read
     * @param fn function which processes the input. The result of the function
     * is ProcessResult which is std::optional<std::string_view>. If the result
     * is empty (it is mean, has_value() is false), then reading continues. If
     * the result is a string_view (even empty), the reading stops and result
     * is used to push_back the data. Function can be called with empty buffer
     * to indicate prematurely end of reading. Even in this case the
     * return value is used, however reading will stop in all cases
     *
     * @retval true read successful
     * @retval false reached end of stream or timeout
     */
    template<typename ProcessFn>
    CXX20_REQUIRES(std::same_as<decltype(std::declval<ProcessFn>()(std::declval<std::string_view>())), ProcessResult>)
    cocls::future<bool>  operator()(const Stream &s, ProcessFn &&fn) {
        return read_mt(_storage, std::forward<ProcessFn>(fn));
    }

    ///Access to mutex.
    auto lock() {
        return _mx.lock();
    }

    template<typename Iterator>
    cocls::future<bool> read_until(const Stream &s,
            Iterator &&write_iter,
            std::string_view pattern,
            std::size_t limit = std::numeric_limits<std::size_t>::max()) {
        if (pattern.empty()) return read_block(s,std::forward<Iterator>(write_iter), limit);
        if (limit == 0) return cocls::future<bool>::set_value(true);
        return (*this)([
                        write_iter = std::move(write_iter),
                        kmp = search_kmp<0>(pattern),
                        state = 0U,
                        limit,
                        count = std::size_t(0)](std::string_view data) mutable ->ProcessResult {
            for(std::size_t i = 0, cnt = data.size(); i<cnt;++i) {
                char c = data[i];
                *write_iter = c;
                ++write_iter;
                if (kmp(state, c) || ++count == limit) {
                    return data.substr(i+1);
                }
            }
            return {};
        });
    }

    template<typename Iterator>
    cocls::future<bool> read_block(const Stream &s,
            Iterator &&write_iter,
            std::size_t limit = std::numeric_limits<std::size_t>::max()) {
        if (limit == 0) return cocls::future<bool>::set_value(true);
        return (*this)([
                        write_iter = std::move(write_iter),
                        limit,
                        count = std::size_t(0)](std::string_view data) mutable ->ProcessResult {

            for(std::size_t i = 0, cnt = data.size(); i<cnt;++i) {
                char c = data[i];
                *write_iter = c;
                ++write_iter;
                if (++count == limit) {
                    return data.substr(i+1);
                }
            }


        });
    }


protected:

    template<typename ProcessFn, typename Allocator>
    cocls::with_allocator<Allocator, cocls::async<bool> > read_mt(Stream s, ProcessFn fn) {
        auto own = co_await _mx.lock();
        std::string_view txt;
        do {
            txt = co_await s.read();
            ProcessResult res = fn(txt);
            if (!res.has_value()) {
                s.put_back(*res);
                co_return true;
            }
        } while (!txt.empty());
        co_return false;
    }

    cocls::mutex _mx;
    cocls::reusable_storage_mtsafe _storage;

};

///Stream which can be read and write by multiple threads
class StreamMT {
    class Internal {
    public:
        Internal(Stream s):_writer(s) {}

        cocls::async<void> destroy() {
            cocls::future<void> f = _writer.wait_for_idle();
            auto ownership = co_await _reader.lock();
            co_await f;
            co_await ownership.release();
            delete this;
            co_return;
        }

        MTStreamWriter _writer;
        ReadFromStreamMT _reader;
    };

public:
    StreamMT(Stream s)
        :_ptr(new Internal(s), Deleter{}) {}

    template<typename ProcessFn>
    cocls::future<bool>  read(const Stream &s, ProcessFn &&fn) {
        return _ptr->_reader(s,std::forward<ProcessFn>(fn));
    }
    template<typename Iterator>
    cocls::future<bool> read_until(const Stream &s,
            Iterator &&write_iter,
            std::string_view pattern,
            std::size_t limit = std::numeric_limits<std::size_t>::max()) {
        return _ptr->_reader.read_until(s,std::forward<Iterator>(write_iter), pattern,limit);
    }
    template<typename Iterator>
    cocls::future<bool> read_block(const Stream &s,
            Iterator &&write_iter,
            std::size_t limit = std::numeric_limits<std::size_t>::max()) {
        return _ptr->_reader.read_block(s,std::forward<Iterator>(write_iter), limit);
    }


    bool write(const std::string_view &text) {
        return _ptr->_writer(text);
    }
    bool write_eof() {
        return _ptr->_writer.write_eof();
    }
    std::size_t get_buffered_size() const {
        return _ptr->_writer.get_buffered_size();
    }
    cocls::future<void> wait_for_flush() {
        return _ptr->_writer.wait_for_flush();
    }
    cocls::future<void> wait_for_idle() {
        return _ptr->_writer.wait_for_idle();
    }

protected:


    std::shared_ptr<Internal> _ptr;

    struct Deleter {
        void operator()(Internal *x) {
            x->destroy().detach();
        }
    };
};


}



#endif
