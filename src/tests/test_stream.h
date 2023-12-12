/*
 * test_stream.h
 *
 *  Created on: 8. 4. 2023
 *      Author: ondra
 */

#ifndef SRC_TESTS_TEST_STREAM_H_
#define SRC_TESTS_TEST_STREAM_H_

#include <coroserver/stream.h>

#include <queue>
#include <thread>

template<std::size_t async_delay_ms = 0>
class TestStream: public coroserver::AbstractStream {
public:
    TestStream(std::vector<std::string> data, std::string *out = nullptr)
    :_out(out)
    {
        for (auto c: data) _data.push(std::move(c));
    }

    virtual coroserver::TimeoutSettings get_timeouts() override {
        return {};
    }
    virtual coroserver::PeerName get_peer_name() const override {
        return {};
    }
    virtual coro::future<std::string_view> read() override {
        if (async_delay_ms) {
            return [&](auto promise) {
                std::thread thr([this,promise = std::move(promise)]() mutable {
                    std::this_thread::sleep_for(std::chrono::milliseconds(async_delay_ms));
                    promise(read_nb());
                });
                thr.detach();
            };
        } else
            return read_nb();
    }
    virtual std::string_view read_nb() override {
        auto sub = read_putback_buffer();
        if (!sub.empty()) return sub;
        if (_data.empty()) return {};
        _tmp = std::move(_data.front());
        _data.pop();
        _cntr.read+=_tmp.size();
        return _tmp;
    }
    virtual coro::future<bool> write(std::string_view s) override {
        if (_out) {
            if (async_delay_ms) {
                return [&](auto promise) {
                    std::thread thr([this,s,promise = std::move(promise)]() mutable {
                        std::this_thread::sleep_for(std::chrono::milliseconds(async_delay_ms));
                        _cntr.write+=s.size();
                        _out->append(s);
                        promise(true);
                    });
                    thr.detach();
                };

            } else {
                _cntr.write+=s.size();
               _out->append(s);
               return true;
            }
        } else {
            return false;
        }
    }
    virtual bool is_read_timeout() const override {
        return false;
    }
    virtual coro::future<bool> write_eof() override {
        return _out != nullptr;
    }
    virtual void set_timeouts(const coroserver::TimeoutSettings &) override {

    }
    virtual void shutdown() override {}

    virtual Counters get_counters() const noexcept override  {
        return _cntr;
    }


    static coroserver::Stream create(std::vector<std::string> data, std::string *out = nullptr) {
        return coroserver::Stream(std::make_shared<TestStream>(std::move(data), out));
    }

protected:
    std::string _tmp;
    std::string *_out;
    std::queue<std::string> _data;
    Counters _cntr;

};




#endif /* SRC_TESTS_TEST_STREAM_H_ */
