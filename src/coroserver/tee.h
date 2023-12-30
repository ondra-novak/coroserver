/*
 * tee.h
 *
 *  Created on: 26. 12. 2023
 *      Author: ondra
 */

#ifndef SRC_COROSERVER_TEE_H_
#define SRC_COROSERVER_TEE_H_
#include "stream.h"

namespace coroserver {

template<std::invocable<bool, std::string_view> Fn>
class Tee: public AbstractProxyStream {
public:

    Tee(Stream s, Fn &&fn): AbstractProxyStream(s.getStreamDevice())
            ,_fn(std::forward<Fn>(fn))  {
        coro::target_simple_activation(_target, [&](auto *fut){
            try {
                std::string_view data = fut->get();
                _fn(false, data);
                _prom(data);
            } catch (...) {
                _prom.reject();
            }
        });

    }

    virtual coro::future<std::basic_string_view<char, std::char_traits<char> > > read() override {
        auto s = read_putback_buffer();
        if (!s.empty()) return s;
        return [&](auto promise) {
            _prom = std::move(promise);
            _fut << [&]{return _proxied->read();};
            _fut.register_target(_target);
        };

    }
    virtual coro::future<bool> write(std::string_view buffer) override {
        if (!buffer.empty()) _fn(true, buffer);
        return _proxied->write(buffer);
    }
    virtual coro::future<bool> write_eof() override {
        _fn(true, std::string_view());
        return _proxied->write_eof();
    }

    virtual std::string_view read_nb() override {
        auto s = this->read_putback_buffer();
        if (!s.empty()) return s;
        s = _proxied->read_nb();
        if (!s.empty()) {
            _fn(false, s);
        }
        return s;
    }

protected:
    std::decay_t<Fn> _fn;

    coro::future<std::string_view> _fut;
    coro::future<std::string_view>::target_type _target;
    coro::promise<std::string_view> _prom;
};


template<std::invocable<bool, std::string_view> Fn>
Stream create_tee(Stream s, Fn &&fn) {
    return Stream(std::make_shared<Tee<Fn> >(s, std::forward<Fn>(fn)));
}

struct TeePeekToStream {
public:
    TeePeekToStream(std::ostream &os):os(os) {}
    TeePeekToStream(const TeePeekToStream &x):os(x.os) {}

    void operator()(bool dir, std::string_view str) {
        std::lock_guard lk(mx);
        os << (dir?"> ":"< ");
        for (char ch : str) {
            if (isprint(static_cast<unsigned char>(ch))) {
                os << ch;
            } else {
                os << "\\x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
            }
        }
        os << "\n";
    }

protected:
    std::mutex mx;
    std::ostream &os;
};



}

#endif /* SRC_COROSERVER_TEE_H_ */
