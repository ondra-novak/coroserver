#pragma once
#ifndef _SRC_COROSERVER_MT_STREAM_qoiwujdoiqjdoiqdjoq_
#define _SRC_COROSERVER_MT_STREAM_qoiwujdoiqjdoiqdjoq_
#include "stream.h"

#include <coro.h>
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
        :stream(std::move(device)) {
        init();
    }

    ///Construct the object
    /**
     * @param s stream
     */
    MTStreamWriter(Stream s):MTStreamWriter(s.getStreamDevice()) {
        init();
    }

    using FnPrototype = decltype([](char){});

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
    template<std::invocable<FnPrototype> Fn>
    bool operator()(Fn &&fn) {
        std::unique_lock lk(_mx);
        if (_e) std::rethrow_exception(_e);
        if (_closed) return false;
        fn([&](char c){_prepared.push_back(c);});
        if (_pending) return true;
        _pending = true;
        std::swap(_prepared,_pending_write);
        on_flush(lk);
        _write_fut << [&]{return stream->write({_pending_write.data(),_pending_write.size()});};
        _write_fut.register_target(_write_fut_target);
        return true;
    }


    ///write prepared message
    /**
     * @param obj data to write
     *
     * @retval true data written to buffer
     * @retval false write is impossible
     * @exception any any exception captured during recent flush
     */
    bool operator()(std::string_view txt) {
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
    coro::future<void> wait_for_idle() {
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
    coro::future<void> wait_for_flush() {
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
        _write_fut << [&]{return stream->write_eof();};
        _write_fut.register_target(_write_fut_target);
        return p;
    }

    ///Destroyes the object. Ensure, that there is no pending operation
    /**
     * You can destroy the object when there is no pending operation. Use
     * wait_for_idle() to synchronize with this state
     */
    ~MTStreamWriter() {
//        assert(!_pending && "Destroying object with pending operation. Use wait_for_idle() to avoid this assert");
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
    std::shared_ptr<IStream> stream;
    mutable std::mutex _mx;
    std::vector<char> _prepared;
    std::vector<char> _pending_write;
    std::vector<std::pair<bool,coro::promise<void> > > _waiting;
    bool _closed = false;
    bool _pending = false;
    bool _write_eof = false;
    bool _destroy_on_done = false;
    std::exception_ptr _e;
    coro::future<bool> _write_fut;
    coro::future<bool>::target_type _write_fut_target;



    void init() {
        coro::target_member_fn_activation<&MTStreamWriter::finish_write>(_write_fut_target, this);
    }
    void finish_write(coro::future<bool> *val) noexcept {
        std::unique_lock lk(_mx);
        try {
            _pending_write.clear();
            _closed = !*val;
            if (_closed) {
                _prepared.clear();
                _pending = false;
                on_idle(lk);
            } else if (_prepared.empty()) {
                _pending = false;
                on_idle(lk);
            } else {
                std::swap(_pending_write,_prepared);
                on_flush(lk); //unlock the lock
                _write_fut << [&]{return stream->write({_pending_write.data(),_pending_write.size()});};
                _write_fut.register_target(_write_fut_target);
            }
        } catch (...) {
            _e = std::current_exception();
            _prepared.clear();
            _pending = false;
            _closed = true;
            on_idle(lk);
        }
        if (_destroy_on_done) {
            lk.unlock();
            delete this;
        }
    }


    void on_idle(std::unique_lock<std::mutex> &lk) {
        auto ntf = reinterpret_cast<coro::promise<void>::pending_notify *>(
                alloca(sizeof(coro::promise<void>::pending_notify)*_waiting.size()));
        std::size_t cnt = 0;
        for (auto &x: _waiting) {
            std::construct_at(ntf+cnt,x.second());
            ++cnt;
        }
        lk.unlock();
        for (std::size_t i = 0; i<cnt; ++i) {
            std::destroy_at(ntf+i);
        }
    }
    void on_flush(std::unique_lock<std::mutex> &lk) {
        auto ntf = reinterpret_cast<coro::promise<void>::pending_notify *>(
                alloca(sizeof(coro::promise<void>::pending_notify)*_waiting.size()));
        std::size_t cnt = 0;
        _waiting.erase(std::remove_if(_waiting.begin(), _waiting.end(),[&](auto &p){
            if (p.first) return false;
            std::construct_at(ntf+cnt, p.second());
            return true;
        }), _waiting.end());
        lk.unlock();
        for (std::size_t i = 0; i<cnt; ++i) {
            std::destroy_at(ntf+i);
        }
    }


    void destroy() {
        std::unique_lock lk(_mx);
        if (_pending) {
            _destroy_on_done = true;
        } else {
            lk.unlock();
            delete this;
        }
    }

    struct Deleter {
        void operator()(MTStreamWriter *inst) {
            inst->destroy();
        }
    };



};

}



#endif
