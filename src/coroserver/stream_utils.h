#pragma once
#ifndef SRC_COROSERVER_STREAM_UTILS_H_
#define SRC_COROSERVER_STREAM_UTILS_H_


#include "character_io.h"

namespace coroserver {

template<typename Stream>
class BlockReader {
public:
    BlockReader(Stream s):_s(std::move(s)) {}

    coro::future<std::string_view> operator()(std::size_t sz) {
        return read(sz);
    }

    coro::future<std::string_view> read(std::size_t sz) {
        return [&](auto promise) {
            _sz  = sz;
            _buffer.clear();
            _result = std::move(promise);
            coro::target_simple_activation(_target, [&](auto){on_read_first();});
            _read_fut << [&]{return _s.read();};
            _read_fut.register_target(_target);
        };
    }


protected:
    Stream _s;
    std::size_t _sz;
    std::vector<char> _buffer;
    coro::promise<std::string_view> _result;
    coro::future<std::string_view> _read_fut;
    coro::future<std::string_view>::target_type _target;

    void on_read_first() {
        try {
            std::string_view data = _read_fut;
            if (data.empty()) {
                _result();
                return;
            }
            if (data.size() >= _sz) {
                auto sub = data.substr(0, _sz);
                auto remain = data.substr(sub.size());
                _s.put_back(remain);
                _result(sub);
                return;
            }
            if (_sz < static_cast<std::size_t>(-65536)) {
                _buffer.reserve(_sz);
            }
            _buffer.resize(data.size());
            std::copy(data.begin(), data.end(), _buffer.begin());
            coro::target_simple_activation(_target, [&](auto){on_read_other();});
            _read_fut << [&]{return _s.read();};
            _read_fut.register_target(_target);
        } catch (...) {
            _result.reject();
        }
    }

    void on_read_other() {
        try {
            while (true) {
                std::string_view data = _read_fut;
                auto remain = _sz - _buffer.size();
                auto b = data.substr(0, remain);
                auto c = data.substr(b.size());
                _s.put_back(c);
                auto curpos = _buffer.size();
                _buffer.resize(curpos+b.size());
                std::copy(b.begin(), b.end(), _buffer.begin()+curpos);
                if (_buffer.size() == _sz || data.empty()) {
                    _result(_buffer.data(), _buffer.size());
                    return;
                }
                _read_fut << [&]{return _s.read();};
                if (_read_fut.register_target_async(_target)) return;
            }
        } catch (...) {
            _result.reject();
        }
    }


};

template<typename Stream>
BlockReader(Stream s) -> BlockReader<Stream>;

template<typename Stream>
class ReadUntil {
public:

    ReadUntil(Stream s, std::size_t limit = -1):_s(std::move(s)),_limit(limit) {}


    template<unsigned int N>
    coro::future<std::string_view> read(const kmp_pattern<char, N> &pattern, bool include_separator = false) {
        _srch = kmp_search<char>(pattern);
        _include_separator = include_separator;
        return [&](auto promise) {
            _buffer.clear();
            _result = std::move(promise);
            coro::target_simple_activation(_target, [&](auto){on_read_first();});
            _read_fut << [&]{return _s.read();};
            _read_fut.register_target(_target);
        };
    }

    template<unsigned int N>
    coro::future<std::string_view> operator()(const kmp_pattern<char, N> &pattern, bool include_separator = false) {
        return read(pattern, include_separator);
    }


protected:
    Stream _s;
    std::size_t _limit;
    kmp_search<char> _srch;
    std::vector<char> _buffer;
    coro::promise<std::string_view> _result;
    coro::future<std::string_view> _read_fut;
    coro::future<std::string_view>::target_type _target;
    bool _include_separator = false;

    void on_read_first() {
        try {
            std::string_view data = _read_fut;
            if (data.empty()) {
                _result();
                return;
            }
            std::size_t pos = 0;
            for (char c: data) {
                ++pos;
                if (_srch(c)) {
                    _s.put_back(data.substr(pos));
                    _result(strip_separator(data.substr(0, pos)));
                    return;
                }
            }
            if (data.size() > _limit) {
                _result.drop();
                return;
            }
            _buffer.resize(data.size());
            std::copy(data.begin(), data.end(), _buffer.begin());
            coro::target_simple_activation(_target, [&](auto){on_read_other();});
            _read_fut << [&]{return _s.read();};
            _read_fut.register_target(_target);
        } catch (...) {
            _result.reject();
        }
    }

    void on_read_other() {
        try {
            while (true) {
                std::string_view data = _read_fut;
                if (data.empty()) {
                    _result(_buffer.data(), _buffer.size());
                    return;
                }
                std::size_t pos = 0;
                for (char c: data) {
                    ++pos;
                    _buffer.push_back(c);
                    if (_srch(c)) {
                        _s.put_back(data.substr(pos));
                        _result(strip_separator(std::string_view(_buffer.data(), _buffer.size())));
                        return;
                    }
                    if (_buffer.size() > _limit) {
                        _result.drop();
                        return;
                    }
                }

                _read_fut << [&]{return _s.read();};
                if (_read_fut.register_target_async(_target)) return;
            }
        } catch (...) {
            _result.reject();
        }
    }

    std::string_view strip_separator(std::string_view s) {
        if (_include_separator) return s;
        return s.substr(0,s.length() - _srch.size());
    }

};

template<typename Stream>
ReadUntil(Stream s) -> ReadUntil<Stream>;



}



#endif /* SRC_COROSERVER_STREAM_UTILS_H_ */
