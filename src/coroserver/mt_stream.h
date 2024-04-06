#pragma once
#ifndef _SRC_COROSERVER_MT_STREAM_qoiwujdoiqjdoiqdjoq_
#define _SRC_COROSERVER_MT_STREAM_qoiwujdoiqjdoiqdjoq_
#include "stream.h"
#include "atomic_mutex.h"

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

    }

    ///Construct the object
    /**
     * @param s stream
     */
    MTStreamWriter(Stream s):MTStreamWriter(s.getStreamDevice()) {

    }

    using WriteIterator = std::back_insert_iterator<std::vector<char> >;


    ///write to buffer and eventually send written data
    /**
     * @param fn function which receives output iterator, which can be used to write data
     * to the buffer
     * @return optional future, which is resolved, when data are written to the
     * stream (they are left buffer). The future can be discarded. Return value of the future
     * is equal to true, when success or false when stream is closed or error. The
     * future can also throw an exception
     *
     * @note MT Safe.
     * @note there is a lock inside of the function
     */
    template<std::invocable<WriteIterator> Fn>
    coro::deferred_future<bool> write(Fn &&fn) {
        coro::promise<bool>::notify ntf;
        std::unique_lock lk(_mx);
        if (!_closed) {
            fn(std::back_inserter(_prepared));
            if (!_pending) {
                ntf = do_write();
            }
        }
        return get_return_value();
    }


    ///write prepared message
    /**
     * @param obj data to write
     *
     * @return optional future, which is resolved, when data are written to the
     * stream (they are left buffer). The future can be discarded. Return value of the future
     * is equal to true, when success or false when stream is closed or error. The
     * future can also throw an exception
     *
     * @exception any any exception captured during recent flush
     */
    coro::deferred_future<bool> write(std::string_view txt) {
        return write([&](auto iter){
            std::copy(txt.begin(), txt.end(), iter);
        });
    }


    ///Returns true, if writing is possible
    operator bool () const {
        std::lock_guard _(_mx);
        return !_closed;
    }

    ///Retrieves total size in bytes in buffer waiting to be send
    /**
     * @return total size of all currently active buffers represents amount
     * of pending bytes
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
     *
     * @return deferred future is resolved when all pending data are written
     */
    coro::deferred_future<bool> close() {
        std::lock_guard _(_mx);
        _closed = true;
        return get_return_value();

    }

    ///Write eof to the output stream
    /**
     * You can call this function even if there are pending writes. This
     * request is buffered and eof is written when all pending writes
     * are complete.
     *
     * @note stream is closed after this call
     *
     * @return deferred future is resolved when all pending data are written and
     * eof is sent
     */
    coro::deferred_future<bool> write_eof() {
        coro::promise<bool>::notify ntf;
        std::unique_lock lk(_mx);
        if (!_closed) {
            _write_eof = true;
            _closed = true;
            if (!_pending) {
                ntf = do_write();
            }
        }
        return get_return_value();
    }

    auto getStreamDevice() const {
        return stream;
    }


protected:
    struct NotifyInfo {
        long _counter;
        coro::promise<bool> _prom;
        static bool cmp(const NotifyInfo &a, const NotifyInfo &b) {
            return a._counter > b._counter;
        }
    };


    std::shared_ptr<IStream> stream;
    mutable std::mutex _mx;
    std::vector<char> _prepared;    //<prepared buffer
    std::vector<char> _pending_write; //<pending write buffer
    std::vector<NotifyInfo> _notify;
    long _counter = 0;
    bool _closed = false;
    bool _pending = false;
    bool _write_eof = false;
    bool _destroy_on_done = false;
    coro::future<bool> _write_fut;

    void reg_promise(long cntr, coro::promise<bool> prom) {
        if (_counter >= cntr) {
            prom(!_closed);
        } else {
            _notify.push_back({cntr, std::move(prom)});
            std::push_heap(_notify.begin(), _notify.end(), NotifyInfo::cmp);
        }
    }

    coro::promise<bool>::notify notify_done() {
        coro::promise<bool> p;
        if (_notify.empty() || _notify.front()._counter > _counter) return {};
        while (!_notify.empty() && _notify.front()._counter <= _counter) {
            p += _notify.front()._prom;
            std::pop_heap(_notify.begin(), _notify.end(), NotifyInfo::cmp);
            _notify.pop_back();
        }
        return p(true);
    }

    coro::promise<bool>::notify notify_error(bool except) {
        coro::promise<bool> p;
        for (auto &x: _notify) p+=x._prom;
        _notify.clear();
        if (except) return p.reject();
        else return p(false);
    }

    coro::deferred_future<bool> get_return_value() {
        double newcnt = _counter+_prepared.size() + _pending_write.size() + _write_eof;
        return [this,newcnt](auto promise)  {
            reg_promise(newcnt, std::move(promise));
        };
    }

    coro::promise<bool>::notify do_write() {
        std::swap(_prepared, _pending_write);
        if (!_pending_write.empty()) {
            _pending = true;
            _write_fut << [&]{return stream->write({_pending_write.data(),_pending_write.size()});};
        } else if (_write_eof) {
            _pending = true;
            _write_fut << [&]{return stream->write_eof();};
        } else {
            return notify_done();
        }
        if (_write_fut.set_callback([this] {
                            std::unique_lock lk(_mx);
                            return finish_write(); })) return notify_done();
        return finish_write();
    }

    coro::promise<bool>::notify finish_write() noexcept {
        _pending = false;
        try {
            bool b = _write_fut;
            if (b) {
                if (_pending_write.empty()) {
                    ++_counter;
                    _write_eof = false;
                    _closed = true;
                    return notify_done();
                } else {
                    _counter += _pending_write.size();
                    _pending_write.clear();
                    return do_write();
                }
            } else {
                _closed = true;
                return notify_error(false);
            }

        } catch (...) {
            _closed = true;
            return notify_error(true);
        }
    }


};


}
#endif
