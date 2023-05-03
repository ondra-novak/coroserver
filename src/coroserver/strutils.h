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

template<std::size_t N>
class search_kmp {
public:

    constexpr search_kmp(std::string_view pattern):_len(std::min(N,pattern.length())) {
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
    using State = std::size_t;

    constexpr std::size_t length() const {
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
    int _lps[N] = {};
    std::size_t _len;
};

class search_pattern {
public:
    search_pattern(std::string_view pattern):_pattern(pattern),_j(0) {
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
    }
    bool operator()(char c) {
        while (_j > 0 && _pattern[_j] != c)
            _j = lps[_j - 1];
        if (_pattern[_j] == c) _j++;

        if (_j == _pattern.length()) {
            _j = 0;
            return true;
        }
        return false;
    }
    std::size_t length() {
        return _pattern.length();
    }
    void reset() {
        _j = 0;
    }
protected:
    std::string _pattern;
    std::size_t _j;
    std::basic_string<unsigned char> lps;
};


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
    auto iter = text.begin();
    auto iend = text.end();
    int b=0,c=0,d=0,e=0;


    while  (iter != iend || (e & 0x80) != 0) {
        b = table.revtable[static_cast<unsigned char>(*iter++)];
        if (iter == iend || (b & 0x80) != 0) break;
        c = table.revtable[static_cast<unsigned char>(*iter++)];
        output(static_cast<char>((b<<2) | ((c & 0x3F) >> 4)));  //bbbbbbcc
        if (iter == iend || (c & 0x80) != 0 ) break;
        d = table.revtable[static_cast<unsigned char>(*iter++)];
        output(static_cast<char>(((c<<4) | ((d & 0x3F) >> 2))& 0xFF)); //ccccdddd
        if (iter == iend || (d & 0x80) != 0 ) break;
        e = table.revtable[static_cast<unsigned char>(*iter++)];
        output(static_cast<char>(((d<<6) | (e & 0x3F))& 0xFF));  //ddeeeeee
    }

}

}

}



#endif /* SRC_COROSERVER_STRUTILS_H_ */
