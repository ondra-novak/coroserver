#pragma once
#include <cstdint>
#include <string_view>
#include <memory>

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
            _dynamic_lps.lps = new unsigned int[_pattern.size()];
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
        unsigned int *lps;
    };

    std::basic_string_view<T> _pattern;
    union {
        StaticLPS _static_lps;
        DynamicLPS _dynamic_lps;
    };
    unsigned int _state = 0;

    template<typename X>
    void build_lps(X *lps) {
        unsigned int size = static_cast<unsigned int>(_pattern.size());
        unsigned int i = 1;
        unsigned int len = 0;
        lps[0] = 0;
        while (i < size) {
            if (_pattern[i] == _pattern[len]) {
                len++;
                lps[i] = static_cast<X>(len);
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


}
