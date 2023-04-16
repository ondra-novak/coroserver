/*
 * strutils.h
 *
 *  Created on: 16. 4. 2023
 *      Author: ondra
 */

#ifndef SRC_COROSERVER_STRUTILS_H_
#define SRC_COROSERVER_STRUTILS_H_
#include <string_view>

namespace coroserver {


inline auto search_pattern(std::string_view pattern) {
    std::basic_string<unsigned char> lps;
    lps.resize(pattern.length());
    unsigned char i = 1;
    unsigned char len = 0;
    lps[0]=0;
    while (i < pattern.length()) {
        if (pattern[i] == pattern[len]) {
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
    return [lps = std::move(lps), j = std::size_t(0), pattern](char c) mutable {
        while (j > 0 && pattern[j] != c)
            j = lps[j - 1];
        if (pattern[j] == c) j++;

        if (j == pattern.length()) {
            j = 0;
            return true;
        }
        return false;
    };
}



inline auto splitAt(std::string_view src, std::string_view pattern) {
    auto srch = search_pattern(pattern);
    using Srch = decltype(srch);
    auto len = pattern.length();

    class Impl {
    public:
        Impl(Srch srch, std::size_t len, std::string_view src)
            :_srch(std::move(srch)), _len(len), _src(src) {}
        operator bool() const {return !_src.empty();}
        operator std::string_view() const {return _src;}
        std::string_view operator()() {
            std::size_t cnt = _src.length();
            for (std::size_t i = 0; i < cnt; i++) {
                if (_srch(_src[i])) {
                    std::string_view out = _src.substr(0, i - _len+1);
                    _src = _src.substr(i+1);
                    return out;
                }
            }
            return  std::exchange(_src, std::string_view());
        }

    protected:
        Srch _srch;
        std::size_t _len;
        std::string_view _src;
    };
    return Impl(srch, len, src);
}




inline std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(s.front())) {
        s = s.substr(1);
    }
    while (!s.empty() && std::isspace(s.back())) {
        s = s.substr(0,s.length()-1);
    }
    return s;
}


}



#endif /* SRC_COROSERVER_STRUTILS_H_ */
