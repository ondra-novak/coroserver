#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace coroserver {

template<typename T>
class pattern_search {
public:

    pattern_search(std::basic_string_view<T> pattern)
        :_pattern(pattern) {
        if (_pattern.size() < 16) {
            _static_lps.dyn_flag = false;
            build_lps(_static_lps.lps);
        } else {
            _dynamic_lps.dyn_flag = true;
            _dynamic_lps.lps = new int[_pattern.size()];
            build_lps(_dynamic_lps.lps);
        }
    }

    pattern_search(const pattern_search &other):_pattern(other._pattern),_state(other._state) {
        if (other._dynamic_lps_lps._dyn_flag) {
            std::construct_at(&_dynamic_lps, other._dynamic_lps);
            _dynamic_lps.lps = new int[_pattern.size()];
            std::copy(other._dynamic_lps.lps,other._dynamic_lps.lps+_pattern.size(),
                    _dynamic_lps.lps);
        } else {
            std::construct_at(&_static_lps, other._static_lps);
        }
    }
    pattern_search(pattern_search &&other):_pattern(other._pattern),_state(other._state) {
        if (other._dynamic_lps.dyn_flag) {
            std::construct_at(&_dynamic_lps, other._dynamic_lps);
            other._dynamic_lps.dyn_flag = false;
        } else {
            std::construct_at(&_static_lps, other._static_lps);
        }
    }
    pattern_search &operator=(const pattern_search &other) {
        if (this != &other) {
            std::destroy_at(this);
            std::construct_at(this, other);
        }
        return *this;
    }
    pattern_search &operator=(pattern_search &&other) {
        if (this != &other) {
            std::destroy_at(this);
            std::construct_at(this, std::move(other));
        }
        return *this;
    }
    ~pattern_search() {
        if (_dynamic_lps.dyn_flag) delete [] _dynamic_lps.lps;
    }

    void reset() {
        _state = 0;
    }

    bool operator()(const T &c) {
        if (_dynamic_lps.dyn_flag) [[unlikely]] return test(c, _dynamic_lps.lps);
        else return test(c, _static_lps.lps);
    }

    std::size_t size() const {return _pattern.size();}


protected:
    struct StaticLPS {
        bool dyn_flag;
        std::uint8_t lps[15];
    };
    struct DynamicLPS {
        bool dyn_flag;
        int *lps;
    };

    std::basic_string_view<T> _pattern;
    union {
        StaticLPS _static_lps;
        DynamicLPS _dynamic_lps;
    };
    unsigned int _state = 0;

    template<typename X>
    void build_lps(X *lps) {
        unsigned int size = _pattern.size();
        unsigned int i = 1;
        unsigned int len = 0;
        lps[0] = 0;
        while (i < size) {
            if (_pattern[i] == _pattern[len]) {
                len++;
                lps[i] = len;
                i++;
            } else if (len != 0) {
                len = lps[len - 1];
            } else {
                lps[i] = 0;
                i++;
            }
        }
    }

    template<typename X>
    bool test(const T &c, const X *lps) {
        while (_state > 0 && _pattern[_state] != c)
            _state = lps[_state - 1];
        if (_pattern[_state] == c) ++_state;

        if (_state == _pattern.size()) {
            _state = 0;
            return true;
        }
        return false;
    }

};


#if 0

template<typename T, unsigned int chrcnt>
class pattern_search {
public:


    static constexpr unsigned int eof_lps = ~static_cast<unsigned int>(0);

    constexpr pattern_search(const T *pattern) {
        unsigned int sz = 0;
        while (pattern[sz]) {
            _pattern[sz] = pattern[sz];
            ++sz;
        }
        build_lps(sz);
    }
    constexpr pattern_search(const T *pattern, std::size_t sz)
        :_pattern(pattern)
    {
        if (sz > chrcnt) sz = chrcnt;
        std::copy(pattern, pattern+sz, _pattern);
        build_lps(sz);
    }

    constexpr pattern_search(std::basic_string_view<T> str)
        :pattern_search(str.data(), str.size()) {}


    constexpr std::size_t size() const {
        std::size_t n = chrcnt;
        while (n && _lps[n-1] == eof_lps) --n;
        return n;
    }

    using state = unsigned int;

    state begin_search() const {
        return state(0);
    }

    bool test(const T &c, unsigned int &state) const {
        while (state > 0 && _pattern[state] != c)
            state = _lps[state - 1];
        if (_pattern[state] == c) ++state;

        if (state == chrcnt || _lps[state] == eof_lps) {
            state = 0;
            return true;
        }
        return false;

    }

protected:
    T _pattern[chrcnt] = {};
    unsigned int _lps[chrcnt] = {};

    unsigned int str_len(const T *pattern) {
        unsigned int l = 0;
        while (pattern[l] && l < chrcnt) ++l;
        return l;
    }

    constexpr void build_lps(unsigned int size) {
        unsigned int i = 1;
        unsigned int len = 0;
        _lps[0]=0;
        while (i < size) {
            if (_pattern[i] == _pattern[len]) {
                len++;
                _lps[i] = len;
                i++;
            } else if (len != 0) {
                 len = _lps[len - 1];
            } else {
                 _lps[i] = 0;
                 i++;
            }
        }
        while (size < chrcnt) {
            _lps[size] = eof_lps;
            ++size;
        }
    }
};

template<typename T, int n>
pattern_search(const T (&)[n]) -> pattern_search<T, n>;
#endif

}
