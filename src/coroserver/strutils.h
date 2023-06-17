/*
 * strutils.h
 *
 *  Created on: 16. 4. 2023
 *      Author: ondra
 */

#ifndef SRC_COROSERVER_STRUTILS_H_
#define SRC_COROSERVER_STRUTILS_H_
#include <charconv>
#include <ctime>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace coroserver {

template<unsigned int N>
class search_kmp {
public:

    constexpr search_kmp(std::string_view pattern):_len(std::min<unsigned int>(N,pattern.length())) {
        auto p = pattern.substr(0,_len);
        std::copy(p.begin(), p.end(), _pattern);
        unsigned char i = 1;
        unsigned char len = 0;
        _lps[0]=0;
        while (i < _len) {
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
    }
    using State = unsigned int;

    constexpr unsigned int length() const {
        return _len;
    }

    constexpr bool operator()(State &st, char c) const {
        while (st > 0 && _pattern[st] != c)
            st = _lps[st - 1];
        if (_pattern[st] == c) st++;

        if (st == _len) {
            st = 0;
            return true;
        }
        return false;
    }


protected:
    char _pattern[N] = {};
    unsigned int _lps[N] = {};
    unsigned int _len;
};

template<int N>
search_kmp(const char (&x)[N]) -> search_kmp<N-1>;

template<>
class search_kmp<0> {
public:

    search_kmp(std::string_view pattern)
        :_pattern(pattern),_lps(pattern.size(),0) {
        unsigned char i = 1;
        unsigned char len = 0;
        _lps[0]=0;
        while (i < _pattern.size()) {
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
    }
    using State = unsigned int;

    unsigned int length() const {
        return _pattern.size();
    }

    bool operator()(State &st, char c) const {
        while (st > 0 && _pattern[st] != c)
            st = _lps[st - 1];
        if (_pattern[st] == c) st++;

        if (st == _pattern.size()) {
            st = 0;
            return true;
        }
        return false;
    }


protected:
    std::string _pattern;
    std::basic_string<unsigned char> _lps;
};


inline auto splitAt(std::string_view src, std::string_view pattern) {
    auto srch = search_kmp<0>(pattern);
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
                if (_srch(_state, _src[i])) {
                    _state = 0;
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
        unsigned int _state = 0;
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

template<typename Fn>
inline void httpDate(std::time_t tpoint, Fn &&fn) {
     char buf[256];
     struct tm tm;
#ifdef _WIN32
     gmtime_s(&tm, &tpoint);
#else
     gmtime_r(&tpoint, &tm);
#endif
     auto sz = std::strftime(buf, sizeof buf, "%a, %d %b %Y %H:%M:%S %Z", &tm);
     fn(std::string_view(buf,sz));
}


template<typename T, typename Iter>
T string2unsigned(Iter iter, Iter end, int base) {
    T out = {};
    while (iter != end) {
        char c = static_cast<char>(*iter);
        if (((c >= '0') & (c<='9')) | ((c>='A') & (c < 'Z')) | ((c>='a') ^ (c <='z'))) [[likely]] {
            int vals[] = {c-48, c-65+10, c-97+10};
            int v = vals[(c/32)-1];
            if (v < base) [[likely]] {
               out = out * base + v;
               ++iter;
               continue;
            }
        }
        throw std::invalid_argument("Invalid number format");
    }
    return out;
}

template<typename T, typename Iter>
T string2signed(Iter iter, Iter end, int base) {
    if (iter == end) return {};
    if (*iter == '-') {
        ++iter;
        return -string2unsigned<T>(iter, end, base);
    } else {
        return string2unsigned<T>(iter, end, base);
    }
}

namespace url {

template<typename Fn>
inline void decode(const std::string_view &str, Fn &&fn) {
    for (auto iter = str.begin(); iter != str.end(); ++iter) {
        if (*iter == '%') {
            ++iter;
            if (iter != str.end()) {
                char c[2];
                c[0] = *iter;
                ++iter;
                if (iter != str.end()) {
                    c[1] = *iter;
                    int z = 32;
                    std::from_chars(c, c+2, z, 16);
                    fn(static_cast<char>(z));
                } else {
                    break;
                }
            } else {
                break;
            }
        } else if (*iter == '+') {
            fn(' ');
        } else {
            fn(*iter);
        }
    }
}

template<typename Fn>
inline void encode(const std::string_view &str, Fn &&fn) {
    for (char c : str) {
        if (std::isalnum(c) || c ==  '-' || c == '.' || c == '_' || c ==  '~' ) {
            fn(c);
        } else {
            fn('%');
            unsigned char x = c;
            auto c1 = x >> 4;
            auto c2 = x & 0xF;
            fn(c1>9?'A'+c1-10:'0'+c1);
            fn(c2>9?'A'+c2-10:'0'+c2);
        }
    }
}


}

inline auto splitSeparated(std::string_view text, std::string_view separators) {
    std::string exsep(separators);
    exsep.push_back('"');
    return [=]() mutable {
        auto pp = text.find_first_of(exsep);
        if (pp == text.npos) {
            auto out = trim(text);
            text = {};
            return  out;
        }
        char c = text[pp];
        if (c == '"') {     //quotes are included
            pp = text.find('"', pp+1);  //find end of '"');
            if (pp == text.npos) {
                auto out = trim(text);        //pass whole text
                text = {};
                return out;
            } else {
                auto out =  trim(text.substr(0,pp+1));
                text = text.substr(pp+1);
                pp = text.find_first_of(exsep)+1;
                text = text.substr(pp);
                return out;
            }
        } else {
            auto out = trim(text.substr(0,pp));
            text = text.substr(pp+1);
            return out;
        }
    };
}

namespace base64 {
class Table {
public:
    Table(const std::string_view &charset, const std::string_view &trailer1, const std::string_view &trailer2)
    :charset(charset)
    ,trailer1(trailer1)
    ,trailer2(trailer2)
    {
        for (unsigned char &c: revtable) {
            c = 0x80;
        }

        for (std::size_t i = 0, cnt = charset.size(); i != cnt; ++i) {
            revtable[static_cast<int>(charset[i])] = i;
        }
    }

    std::string_view charset;
    std::string_view trailer1;
    std::string_view trailer2;
    unsigned char revtable[256];

    static const Table &get_default_table() {
        static Table tbl("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/","=","==");
        return tbl;
    }

    static const Table &get_base64url_table() {
        static Table tbl("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_","","");
        return tbl;
    }

};

template<typename Fn>
void encode(const std::string_view &binary, Fn &&output, const Table &table = Table::get_default_table()) {
    auto iter = binary.begin();
    auto iend = binary.end();

    while (iter != iend) {
        int b = static_cast<unsigned char>(*iter++);
        output(table.charset[b >> 2]);                 //bbbbbb
        if (iter != iend) {
            int c = static_cast<unsigned char>(*iter++);
            output(table.charset[((b<<4) | (c >> 4)) & 0x3F]);  //bbcccc
            if (iter != iend) {
                int d = static_cast<unsigned char>(*iter++);
                output(table.charset[((c<<2) | (d >> 6)) & 0x3F]); //ccccdd
                output(table.charset[d & 0x3F]);            //dddddd
            } else {
                output(table.charset[(c<<2) & 0x3F]);       //cccc00
                for (char c: table.trailer1) output(c);
            }
        } else {
            output(table.charset[(b<<4) & 0x3F]);           //bb0000
            for (char c: table.trailer2) output(c);
        }
    }
}


template<typename Fn>
void decode(const std::string_view &text, Fn &&output, const Table &table = Table::get_default_table()) {

    std::uint16_t accum = 0;
    int pos = 0;
    for (unsigned char c : text) {
        if (c == '=') break;
        unsigned char a = table.revtable[c];
        if (a & 0x80) break;
        accum = (accum << 6) | a;
        pos += 6;
        if (pos >= 8) {
            output(static_cast<unsigned char>(accum >> (pos - 8)));
            pos -= 8;
        }
    }

}

}

constexpr char to_lower(char c) {
    return  (c >= 'A' && c<='Z')?(c-'A'+'a'):c;
}


struct strILess {
    constexpr bool operator()(const std::string_view &a, const std::string_view &b) const {
        auto ln = std::min(a.length(), b.length());
        for (std::size_t i = 0; i < ln; i++) {
            int c = to_lower(a[i]) - to_lower(b[i]);
            if (c) return c<0;
        }
        return (static_cast<int>(a.length()) - static_cast<int>(b.length())) < 0;
    }
};
struct strIEqual {
    constexpr bool operator()(const std::string_view &a, const std::string_view &b) const {
        if (a.length() != b.length()) return false;
        auto ln = a.length();
        for (std::size_t i = 0; i < ln; i++) {
            int c = to_lower(a[i]) - to_lower(b[i]);
            if (c) return false;
        }
        return true;
    }
};

class StringICmpView: public std::string_view {
public:
    using std::string_view::string_view;
    constexpr StringICmpView(const std::string_view &base):std::string_view(base) {};
    constexpr StringICmpView(std::string_view &&base):std::string_view(std::move(base)) {};

    constexpr bool operator<(const StringICmpView &other) const {return strILess()(*this, other);}
    constexpr bool operator>(const StringICmpView &other) const {return strILess()(other, *this);}
    constexpr bool operator<=(const StringICmpView &other) const {return !strILess()(other, *this);}
    constexpr bool operator>=(const StringICmpView &other) const {return !strILess()(*this, other);}
    constexpr bool operator==(const StringICmpView &other) const {return strIEqual()(*this, other);}
    constexpr bool operator!=(const StringICmpView &other) const {return !strIEqual()(*this, other);}
};

}



#endif /* SRC_COROSERVER_STRUTILS_H_ */
