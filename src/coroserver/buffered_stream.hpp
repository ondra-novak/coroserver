#pragma once

#include "coroutines.hpp"
#include "stream.hpp"
#include <mutex>
#include <vector>
#include <format>
namespace coroserver {

class BufferedStreamImpl: public StreamProxy, public std::enable_shared_from_this<BufferedStreamImpl> {
public:

    using StreamProxy::StreamProxy;
    BufferedStreamImpl(Stream s, std::size_t minbuff)
        :StreamProxy(std::move(s)),_minbuff(minbuff) {}

    bool write_buff(std::string_view text) {
        return write_buff([&](auto iter){
            std::copy(text.begin(), text.end(), iter);
        });
    }

    template<typename ... Args>
    bool format(std::format_string<Args...> fmt, Args && ... args) {
        return write_buff([&](auto iter){
            std::format_to(iter, fmt, std::forward<Args>(args)...);
        });
    }

    template<typename Fn>
    requires(requires(Fn v, std::vector<char> buff){
        {v(std::back_inserter(buff))};
    })
    bool write_buff(Fn &&fn) {
        prepared_coro pc;
        std::lock_guard _(_wrmx);
        if (_closed) return false;
        auto iter = std::back_inserter(_current_buffer);
        fn(iter);
        pc = flush_if_data(_minbuff);
        return true;
    }

    void close_buff() {
        prepared_coro pc;
        std::lock_guard _(_wrmx);
        pc = flush_if_data(0);
        _closed = true;
        if (!pc && !_is_pending) {
            _cb.await(close(),[h = shared_from_this()](auto &){});
        }

    }

    coro::awaitable<bool> flush() {
        prepared_coro pc;
        std::lock_guard _(_wrmx);
        pc = flush_if_data(0);
        if (!_is_pending) return !_closed;
        return [this](coro::awaitable<bool>::result r) {
            std::lock_guard _(_wrmx);
            _flush_pos.push_back({
                _current_buffer.size()+_pending_buffer.size(),
                std::move(r)
            });
        };
    }


    void flush_bg() {
        prepared_coro pc;
        std::lock_guard _(_wrmx);
        pc =  flush_if_data(0);
    }

    std::size_t get_buffered_size() const {
        std::lock_guard _(_wrmx);
        return _current_buffer.size()+_pending_buffer.size();
    }



protected:


    struct FlushNtf {
        std::size_t pos;
        coro::awaitable<bool>::result r;
    };

    mutable std::mutex _wrmx;
    std::vector<char> _current_buffer;
    std::vector<char> _pending_buffer;
    std::vector<FlushNtf> _flush_pos;
    std::shared_ptr<BufferedStreamImpl> _is_pending = {};
    std::size_t _minbuff = 0;
    bool _closed = false;

    coro::awaiting_callback<coro::awaitable<bool>, std::shared_ptr<BufferedStreamImpl> > _cb;

    coro::prepared_coro finish_write(coro::awaitable<bool> &awt) {
        std::unique_lock lk(_wrmx);
        std::size_t wrsz = _pending_buffer.size();
        _pending_buffer.clear();
        try {
            if (!awt.has_value() || !awt.await_resume()) {
                _closed = true;
                return finish_flush(lk, wrsz + _current_buffer.size(), false);
            }
            std::swap(_pending_buffer, _current_buffer);
            if (_pending_buffer.empty()) {
                auto h = std::move(_is_pending);
                _is_pending.reset();
                auto p = finish_flush(lk, wrsz + _current_buffer.size(), true);
                if (_closed) {
                    _cb.await(close(), [h = std::move(h)](auto &) {});
                }
                return p;
            } else {
                auto q = finish_flush(lk, wrsz, true);
                auto p = _cb.await_cont(write(std::string_view(_pending_buffer.data(), _pending_buffer.size())));
                return p?std::move(p):std::move(q);
            }

        } catch (...) {
            auto h = std::move(_is_pending);
            return finish_flush(lk, wrsz + _current_buffer.size(), true);
        }
    }

    coro::prepared_coro finish_flush(std::unique_lock<std::mutex> &lk,
            std::size_t pos, bool status, std::size_t idx = 0) {
        coro::prepared_coro p;
        if (idx >= _flush_pos.size()) {
            lk.unlock();
            return p;
        }
        if (_flush_pos[idx].pos > pos) {
            std::size_t t = 0;
            while (idx+t < _flush_pos.size()) {
                _flush_pos[t] = {
                        _flush_pos[idx].pos - pos,
                        std::move(_flush_pos[idx].r)
                };
                ++t;
            }
            _flush_pos.resize(t);
            lk.unlock();
            return p;
        }
        p = _flush_pos[idx].r(status);
        finish_flush(lk, pos, status, idx+1);
        return p;
    }

    prepared_coro flush_if_data(std::size_t sz) {
        if (_current_buffer.size() < sz || _is_pending) return {};
        _is_pending = shared_from_this();
        _pending_buffer = std::move(_current_buffer);
         return _cb.await(write(std::string_view(_pending_buffer.data(), _pending_buffer.size())),
                [this](auto &awt){return finish_write(awt);});
    }

};

///A stream with unspecified output buffer
/**
 * The output buffer helps to concat small writes into single big continous write.
 * It uses latency of asynchronous processing. It can build a new buffer while previous
 * buffer is still pending.
 *
 * The main benefit of the class is that function write is not awaitable. You
 * can write and continue without need to await for completion. The operation
 * is completed at background. Other benefit is that writing is MT safe.
 */
class BufferedStream: public Stream {
public:

    ///Construct buffered stream from some normal stream
    BufferedStream(Stream s):Stream(std::make_shared<BufferedStreamImpl>(s)) {}

    ///Construct buffered stream
    /**
     * @param s target stream
     * @param size size of buffer. If the buffered size reaches this size,
     * the buffer is flushed at background. This isn't hard limit, the buffer
     * can grow if the data are pushed faster than can be transfered by the stream.
     * However if the buffered size if less than this value, data are not sent waiting
     * for more data to come
     *
     * @note When internal buffer holds less than specified size, it doesn't pass them
     * to the output stream. Once the amount of data reaches specified size, everything
     * is flushed to the output stream including any data stored in the buffer meanwhile.
     *
     *
     */
    BufferedStream(Stream s, std::size_t size):Stream(std::make_shared<BufferedStreamImpl>(s, size)) {}

    ///Write a text to buffered stream.
    /**
     * @param text text to write
     * @retval true success
     * @retval false  not written, stream closed, or error
     */
    bool write(std::string_view text) {
        return std::static_pointer_cast<BufferedStreamImpl>(_ptr)->write_buff(text);
    }

    ///Write formatted output
    /**
     * @param fmt format pattern
     * @param args arguments
     * @retval true success
     * @retval false  not written, stream closed, or error
     */
    template<typename ... Args>
    bool format(std::format_string<Args...> fmt, Args && ... args) {
        return std::static_pointer_cast<BufferedStreamImpl>(_ptr)->format(fmt, std::forward<Args>(args)...);
    }

    ///Write to buffer by using an iterator
    /**
     * @param fn a function which is called with an output iterator. The function can
     * write how many bytes it needs. The written data are sent once the function finishes
     * its execution
     * @retval true stored
     * @retval false, function was not called, stream is closed
     */
    template<typename Fn>
    requires(requires(Fn v, std::vector<char> buff){
        {v(std::back_inserter(buff))};
    })
    bool write(Fn &&fn) {
        return std::static_pointer_cast<BufferedStreamImpl>(_ptr)->write_buff(std::forward<Fn>(fn));
    }

    ///waits until buffered content is sent
    /**
     *
     * If there are some data waiting to be send, they are sent now. The function
     * returns awaitable which is fullfilled when all buffered data
     * are successfuly passed to the output stream or when error condition is reported
     *
     * @return awaitable
     * @retval true sent
     * @retval false there were an error
     */
    coro::awaitable<bool> flush() {
        return std::static_pointer_cast<BufferedStreamImpl>(_ptr)->flush();
    }

    ///Flush at background
    /**
     * Forces any buffered data to be flushed. The flush operation is executed at background
     * you will not receive notification about completion
     */
    void flush_bg() {
        std::static_pointer_cast<BufferedStreamImpl>(_ptr)->flush_bg();
    }

    ///retrieves current buffered size
    std::size_t get_buffered_size() const {
        return std::static_pointer_cast<BufferedStreamImpl>(_ptr)->get_buffered_size();
    }
    ///close output, send EOF.
    /**
     * The function schedule such operation at the end of the current buffer. You
     * can no longer write to the stream, but if there are buffered data, they are
     * all written before the stream is closed
     *
     * @note executes flush at background
     */
    void close() {
        std::static_pointer_cast<BufferedStreamImpl>(_ptr)->close();
    }

};



}

