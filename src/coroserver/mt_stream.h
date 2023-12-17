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



    template<std::invocable<FnPrototype> Fn>
    coro::lazy_future<bool> write(Fn &&fn) {
        std::unique_lock lk(_mx);
        if (_e) std::rethrow_exception(_e);
        if (_closed) return false;
        fn([&](char c){_prepared.push_back(c);});
        if (_pending) {
            lk.release(); //release mutex in locked state - we handle it as lazy target
            return _lazy_write_target2;
        }
        _pending = true;
        std::swap(_prepared,_pending_write);
        if (_pending_write.empty()) {
            return !_closed;
        }
        _write_fut << [&]{return stream->write({_pending_write.data(),_pending_write.size()});};
        if (_write_fut.register_target_async(_write_fut_target)) {
            lk.release(); //release mutex in locked state - we handle it as lazy target
            return _lazy_write_target1;
        }
        _pending = false;
        _pending_write.clear();
        _closed = !_write_fut.get();
        return !_closed;
    }


    ///write prepared message
    /**
     * @param obj data to write
     *
     * @retval true data written to buffer
     * @retval false write is impossible
     * @exception any any exception captured during recent flush
     */
    coro::lazy_future<bool> write(std::string_view txt) {
        return write([&](auto wr){
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
     * @return function returns discardable future. The future always returns false as the
     * stream is closed and no futher writes are posible. You can co_await future to
     * ensure, that internal buffers are emptied
     */
    coro::lazy_future<bool> write_eof() {
        std::unique_lock lk(_mx);
        if (_pending) {
            _write_eof = true;
            _closed = true;
            lk.release();
            return _lazy_write_target2;
        }
        if (_closed) {
            return false;
        }
        _closed = true;
        _write_fut << [&]{return stream->write_eof();};
        if (_write_fut.register_target_async(_write_fut_target)) {
            lk.release();
            return _lazy_write_target1;
        }
        return false;
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
    std::vector<coro::promise<bool> > _flush1_ntf;
    std::vector<coro::promise<bool> > _flush2_ntf;
    bool _closed = false;
    bool _pending = false;
    bool _write_eof = false;
    bool _destroy_on_done = false;
    std::exception_ptr _e;
    coro::future<bool> _write_fut;
    coro::future<bool>::target_type _write_fut_target;
    coro::lazy_future<bool>::promise_target_type _lazy_write_target1;
    coro::lazy_future<bool>::promise_target_type _lazy_write_target2;



    void init() {
        coro::target_member_fn_activation<&MTStreamWriter::finish_write>(_write_fut_target, this);
        coro::target_simple_activation(_lazy_write_target1, [&](auto promise){
            if (promise) _flush1_ntf.push_back(std::move(promise));
            _mx.unlock();
        });
        coro::target_simple_activation(_lazy_write_target2, [&](auto promise){
            if (promise) _flush2_ntf.push_back(std::move(promise));
            _mx.unlock();
        });
    }

    template<typename X, typename ... Ranges>
    static void notify_flush(std::unique_lock<std::mutex> &lk, X &&val, Ranges && ... rngs) {
        using PROM = coro::promise<bool>::pending_notify;
        std::size_t sz = (0 + ... +std::distance(rngs.begin(), rngs.end()));
        auto ntf = reinterpret_cast<PROM *>(alloca(sizeof(PROM)*sz));
        int n = 0;

        auto fill = [&](auto &r) {
            for (auto &p: r) {
                std::construct_at(ntf+n, p(val));
                ++n;
            }
            r.clear();
        };
        (fill(rngs),...);
        lk.unlock();
        for (int i = 0; i < n; ++i) {
            std::destroy_at(ntf+i);
        }
    }

    void finish_write(coro::future<bool> *val) noexcept {
        std::unique_lock lk(_mx);
        try {
            _pending_write.clear();
            _closed = !val->get();
            if (_closed) {
                _prepared.clear();
                _pending = false;
                 notify_flush(lk, true, _flush1_ntf, _flush2_ntf);

            } else if (_prepared.empty()) {
                _pending = false;
                notify_flush(lk, true, _flush1_ntf, _flush2_ntf);
            } else {
                std::swap(_pending_write,_prepared);
                std::swap(_flush1_ntf, _flush2_ntf);
                notify_flush(lk, true, _flush2_ntf);
                _write_fut << [&]{return stream->write({_pending_write.data(),_pending_write.size()});};
                _write_fut.register_target(_write_fut_target);
            }
        } catch (...) {
            _e = std::current_exception();
            _prepared.clear();
            _pending = false;
            _closed = true;
            notify_flush(lk, _e, _flush1_ntf, _flush2_ntf);
        }
        if (_destroy_on_done) {
            lk.unlock();
            delete this;
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
