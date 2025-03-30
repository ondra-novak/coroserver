#pragma once
#include <cstddef>
#include <string_view>

namespace coroserver {

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


}
