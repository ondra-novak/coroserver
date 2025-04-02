#pragma once

#include "stream.hpp"

namespace coroserver {

///Limits reads and writes
/**
 * Useful to support limited streams on HTTP
 */
class LimitedStream : public StreamProxy {
public:

    LimitedStream(Stream src, std::size_t limit_read, std::size_t limit_write)
        :StreamProxy(src), _limit_read(limit_read),_limit_write(limit_write)
    {}

    virtual coro::awaitable<std::string_view> read() override {
        if (!_putback_buff.empty()) return std::exchange(_putback_buff, {});
        auto awt = StreamProxy::read();
        if (awt.await_ready()) {
            return crop_input(awt.await_resume());
        }
        _cb.prepare_await(awt);
        return [this](coro::awaitable<std::string_view>::result r) {
            if (!r) {
                _cb.get_awaiter()->cancel();
                return coro::prepared_coro();
            }
            return _cb.await_on_prepared([this, r = std::move(r)](coro::awaitable<std::string_view> &awt) mutable {
                try {
                    return r(crop_input(awt.await_resume()));
                } catch (...) {
                    return r.set_exception(std::current_exception());
                }
            });
        };
    }
    virtual coro::awaitable<bool> write(std::string_view data) override {
        data = crop_output(data);
        if (data.empty()) return false;
        return write(data);
    }

    virtual coro::awaitable<bool> close() override {
        auto remain = get_remaining_write();
        if (remain) return this->_s.fill(remain, 0);
        else return true;
    }

    virtual void put_back(std::string_view s) override {
        _putback_buff = s;
    }

    std::size_t get_remaining_read() const {return _limit_read-_bytes_read;}
    std::size_t get_remaining_write() const {return _limit_write-_bytes_written;}

    virtual StreamState get_state() const override {
        if (_bytes_read == _limit_read && (_limit_read != 0 || _limit_write == 0)) {
            return StreamState::closed;
        }
        if (_bytes_written == _limit_write && _limit_write != 0) {
            return StreamState::closing;
        }
        return StreamProxy::get_state();
    }

    static Stream create_read_limited(Stream s, std::size_t count) {
        return Stream(std::make_shared<LimitedStream>(std::move(s),count,0));
    } 
    static Stream create_write_limited(Stream s, std::size_t count) {
        return Stream(std::make_shared<LimitedStream>(std::move(s),0,count));
    } 

protected:
    std::size_t _limit_read;
    std::size_t _limit_write;
    std::size_t _bytes_read = 0;
    std::size_t _bytes_written = 0;

    std::string_view _putback_buff;
    coro::awaiting_callback<coro::awaitable<std::string_view>,
            LimitedStream *, coro::awaitable<std::string_view>::result> _cb;

    std::string_view crop_input(std::string_view s) {
        auto z = s.substr(0, _limit_read - _bytes_read);
        StreamProxy::put_back(s.substr(z.size()));
        _bytes_read += z.size();
        return z;
    }

    std::string_view crop_output(std::string_view s) {
        auto z = s.substr(0, _limit_write - _bytes_written);
        _bytes_written += z.size();
        return z;
    }
};


}

