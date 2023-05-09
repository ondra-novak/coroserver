/*
 * serializer.h
 *
 *  Created on: 8. 4. 2023
 *      Author: ondra
 */

#ifndef SRC_COROSERVER_JSON_SERIALIZER_H_
#define SRC_COROSERVER_JSON_SERIALIZER_H_

#include <cocls/future.h>
#include <cocls/async.h>
#include "../character_io.h"
#include "value.h"

#include <cmath>
#include <utility>

#include <stack>
#include <variant>

namespace coroserver{

namespace json {

namespace _details {
    template<typename X>
    inline  void unsigned_to_string(std::string &buff, X number, int places = 1) {
        while (number && places > 0) {
            unsigned_to_string(buff, number/10, places-1);
            buff.push_back((number%10)+'0');
        }
    }
    template<typename X>
    inline  void signed_to_string(std::string &buff, X number, int places = 1) {
        if (number < 0) {
            buff.push_back('-');
            unsigned_to_string(buff, -number, places);
        } else {
            unsigned_to_string(buff, number, places);
        }

    }
    inline void double_to_string(std::string &buff, double x) {
        if (!std::isfinite(x)) {
            if (std::isinf(x)) {
                if (x > 0) buff.append("\"+∞\"");
                else buff.append("\"-∞\"");
            } else {
                buff.append("null");
            }
        } else {
            if (x < 0) {
                buff.push_back('-');
                x = -x;
            }
            double dexp = std::floor(std::log10(x));
            if (dexp > 9 || dexp < -3) {
                x/=std::pow(10, dexp);
            } else {
                dexp = 0;
            }
            double intpart;
            double decpart;
            decpart = std::modf(x,&intpart);

            unsigned int v = static_cast<unsigned int>(intpart);
            if (decpart > 0.999999) {
                ++v;
                decpart = 0;
            }
            unsigned_to_string(buff, v);
            if (decpart > 0.000001) {
                buff.push_back('.');
                int cnt = 15;
                while (cnt && decpart > 0.000001) {
                    decpart = std::modf(decpart*10,&intpart);
                    v = static_cast<unsigned int>(intpart);
                    if (decpart > 0.999999) {
                        ++v;
                        decpart = 0;
                    }
                    buff.push_back(v+'0');
                    cnt--;
                }
            }
            if (dexp) {
                buff.push_back('E');
                signed_to_string(buff,static_cast<int>(dexp));
            }
        }

    }

    inline void flush_unicode2(std::string &buff, int c) {
        buff.push_back('\\');
        buff.push_back('u');
        int u1 = (c >> 12) & 0xF;
        int u2 = (c >> 8) & 0xF;
        int u3 = (c >> 4) & 0xF;
        int u4 = (c     ) & 0xF;
        static char hextable[] = "0123456789abcdef";
        buff.push_back(hextable[u1]);
        buff.push_back(hextable[u2]);
        buff.push_back(hextable[u3]);
        buff.push_back(hextable[u4]);
    }

    inline void flush_unicode(std::string &buff, int c) {
        if (c > 0xFFFF) {
            int high_surrogate = (0xD800 | (c >> 10));
            int low_surrogate = (0xDC00 | (c & 0x3FF));
            flush_unicode2(buff, high_surrogate);
            flush_unicode2(buff, low_surrogate);
        } else {
            flush_unicode2(buff, c);
        }
    }

    inline void string_to_string(std::string &buff, std::string_view x) {
        int unicode = 0;
        buff.push_back('"');
        for (char c : x) {
            if (c & 0x80) {
                if ((c & 0x40) == 0) {
                    unicode = (unicode << 6) | (c & 0x3F);
                } else {
                    if (unicode) flush_unicode(buff, unicode);
                    if ((c & 0x20) == 0) unicode = (c & 0x1F);
                    else if ((c & 0x10) == 0) unicode = (c & 0x0F);
                    else if ((c & 0x08) == 0) unicode = (c & 0x07);
                    else if ((c & 0x04) == 0) unicode = (c & 0x03);
                    else if ((c & 0x02) == 0) unicode = (c & 0x01);
                }
            } else {
                if (unicode) flush_unicode(buff,unicode);
                switch(c) {
                    case '"': buff.push_back('\\');buff.push_back('"');break;
                    case '\\': buff.push_back('\\');buff.push_back('\\');break;
                    case '/': buff.push_back('\\');buff.push_back('/');break;
                    case '\n': buff.push_back('\\');buff.push_back('n');break;
                    case '\r': buff.push_back('\\');buff.push_back('r');break;
                    case '\t': buff.push_back('\\');buff.push_back('t');break;
                    case '\f': buff.push_back('\\');buff.push_back('f');break;
                    case '\b': buff.push_back('\\');buff.push_back('b');break;
                    default: if (c >= '\0' && c < ' ') {
                        flush_unicode2(buff,c);
                    } else {
                        buff.push_back(c);
                    }
                }
            }
        }
        if (unicode) flush_unicode(buff,unicode);
        buff.push_back('"');
    }

}
///JSON serializer - generator-like object which generates characters while it serializes the object
/**
 * Generates a sequence of characters. Generation ends when zero is returned
 */
class Serializer {

    class State;
    using Stack = std::stack<State>;

    enum class QuoteMode {
        no_quotes,
        begin_end_quotes,
        end_quote
    };

    struct Utf8ToUnicode{
        int _unicode = 0;
        int _unicode_len = 0;
        Stack &_st;
        Utf8ToUnicode(Stack &st):_st(st) {}

        char push_string_view(std::string_view text) const {
            _st.push(StateString<std::string_view>{text, QuoteMode::no_quotes});
            return _st.top()(_st);
        }
        char push_string(std::string text) const {
            _st.push(StateString<std::string>{std::move(text), QuoteMode::no_quotes});
            return _st.top()(_st);
        }

        char operator()(unsigned char c) {
            if (c & 0x80) {
                if ((c & 0x40) == 0) {
                    if (_unicode_len) {
                        _unicode = (_unicode << 6) | (c & 0x3F);
                        --_unicode_len;
                    } else {
                        _unicode = c;
                    }
                    if (!_unicode_len) {
                        return true;
                    }
                } else if ((c & 0x20) == 0) {
                    _unicode = (c & 0x1F); _unicode_len = 1;
                } else if ((c & 0x10) == 0) {
                    _unicode = (c & 0x0F); _unicode_len = 2;
                } else if ((c & 0x08) == 0) {
                    _unicode = (c & 0x07); _unicode_len = 3;
                } else if ((c & 0x04) == 0) {
                    _unicode = (c & 0x03); _unicode_len = 4;
                } else if ((c & 0x02) == 0) {
                    _unicode = (c & 0x01); _unicode_len = 5;
                }
                return false;
            } else {
                _unicode = c;
                return true;
            }
        }


        operator char() const {
            switch (_unicode) {
                case '"': return push_string_view( "\\\"");
                case '\\': return push_string_view("\\\\");
                case '/': return  push_string_view("\\/");
                case '\n':return push_string_view("\\n");
                case '\r':return  push_string_view("\\r");
                case '\t':return  push_string_view("\\t");
                case '\f':return  push_string_view("\\f");
                case '\b':return  push_string_view("\\b");
                default:break;
            }
            if (_unicode >=  32 && _unicode < 128) {
                return _unicode;
            } else {
                std::string buff;
                _details::flush_unicode(buff, _unicode);
                return push_string(std::move(buff));
            }
        }
    };

    template<typename T>
    struct StateString {
        T _str;
        QuoteMode _q;
        std::size_t _pos = 0;
        char operator()(Stack &st) {
            switch (_q) {
                case QuoteMode::no_quotes: {
                    char c = _str[_pos++];
                    if (_pos == _str.size()) st.pop();
                    return c;
                }
                case QuoteMode::begin_end_quotes:
                    _q = QuoteMode::end_quote;
                     return '"';
                case QuoteMode::end_quote: if (_pos >= _str.size()) {
                            st.pop();
                            return '"';
                        } else {
                            Utf8ToUnicode conv(st);
                            while (_pos < _str.size() && !conv(_str[_pos++]));
                            return conv;
                        }
                default:
                    return -1;
            }
        }
    };


    struct StateObject {
        Object::const_iterator _pos;
        Object::const_iterator _end;
        int _state = 0;
        char operator()(Stack &st) {
            switch (_state) {
                case 0: _state=_pos == _end?5:1;
                        return '{';
                case 1: st.push(StateString<std::string_view>{_pos->first, QuoteMode::begin_end_quotes});
                        _state++;
                        return st.top()(st);
                case 2: _state++; return ':';
                case 3: st.push(State(_pos->second));
                        _state++;
                        ++_pos;
                        return st.top()(st);
                case 4: if (_pos == _end) {
                            st.pop();
                            return '}';
                        } else {
                            _state = 1;
                            return ',';
                        }
                case 5: st.pop();
                        return '}';
                default:
                        return -1;

            }
        }
    };

    struct StateArray {
        Array::const_iterator _pos;
        Array::const_iterator _end;
        int _state = 0;
        char operator()(Stack &st) {
            switch (_state) {
                case 0: _state=_pos == _end?3:1;
                        return '[';
                case 1: st.push(State(*_pos));
                        _state++;
                        ++_pos;
                        return st.top()(st);
                case 2: if (_pos == _end) {
                            st.pop();
                            return ']';
                        } else {
                            _state = 1;
                            return ',';
                        }
                case 3: st.pop();
                        return ']';
                default:
                        return -1;
            }
        }
    };


    using StateVariant = std::variant<
                           StateString<std::string>,
                           StateString<std::string_view>,
                           StateObject,
                           StateArray
                        >;

    class State: public StateVariant {
    public:
        using StateVariant::StateVariant;
        explicit State(const Value &v):StateVariant(createState(v)) {}

        static StateVariant createState(const Value &v) {
            switch (v.type()) {
                default:
                case Type::undefined: return StateString<std::string_view>{"undefined",QuoteMode::begin_end_quotes};
                case Type::null:
                case Type::boolean:
                case Type::number: return StateString<std::string>{v.get_string(),QuoteMode::no_quotes};
                case Type::string: return StateString<std::string_view>{v.get_string_view(),QuoteMode::begin_end_quotes};
                case Type::object: {
                    const Object &obj = v.get_object();
                    return StateObject{obj.begin(), obj.end()};
                }
                case Type::array: {
                    const Array &arr = v.get_array();
                    return StateArray{arr.begin(), arr.end()};
                }



            };
        }
        char operator()(Stack &st) {
            return std::visit([&](auto &x){
                return x(st);
            }, *this);
        }
    };




public:



    ///Initialize serialize with value
    /**
     * @param v reference to json value
     *
     * @note the value is not stored. The generator just stores
     *  a reference. You have to keep the value valid until generation
     *  ends
     */
    Serializer(const Value &v) {
        _stack.push(State(v));
    }


    ///Initialize empty serializer
    /**
     * This serializer return zero for every call. However, you
     * can assign a value by operator <<
     */
    Serializer() = default;


    ///Start serializing of given value
    /**
     * Operator also resets internal state.
     *
     * @param v reference to json value
     *
     * @note the value is not stored. The generator just stores
     *  a reference. You have to keep the value valid until generation
     *  ends
     */
    void operator << (const Value &v) {
        _stack = {};
        _stack.push(State(v));
    }

    ///Retrieve next character
    /**
     * @return next character, 0 if EOF (as the 0 cannot appear in JSON text)
     */
    char operator()() {
        if (_stack.empty()) return 0;
        return _stack.top()(_stack);
    }

    ///Generates into the string
    std::string to_string() {
        std::string out;
        char i = (*this)();
        while (i) {
            out.push_back(i);
            i = (*this)();
        }
        return out;
    }

    ///Generate to a fixed-length buffer
    /**
     * @tparam n size of the buffer in bytes
     * @param buffer reference to buffer
     * @return count of bytes written. If the returned value is less
     * than n, the generation finished (however, it is possible to test
     * return value for zero, which can be used as completion.
     */
    template<std::size_t n = 16384>
    std::size_t to_buffer(std::array<char, n> &buffer) {
        for (std::size_t i = 0; i < n ; i++) {
            char c = (*this)();
            if (!c) return i;
            buffer[i] = c;
        }
        return n;
    }

    ///Serializing generator - generates strings
    /**
     * @tparam max_buffer_size maximum size of buffer
     * @param v object to serialize. Note that object stored as reference
     * @return generator
     */
    template<size_t max_buffer_size = 16384>
     static cocls::generator<std::string_view> generator(const Value &v) {
         return generator_coro<const Value &, max_buffer_size>(v);
     }
    ///Serializing generator - generates strings
    /**
     * @tparam max_buffer_size maximum size of buffer
     * @param v object to serialize. Object must be moved in (and then
     * it is owned by the generator)
     * @return generator
     */
    template<size_t max_buffer_size = 16384>
     static cocls::generator<std::string_view> generator(Value &&v) {
         return generator_coro<Value, max_buffer_size>(std::move(v));
     }


protected:
    template<typename V, size_t max_buffer_size = 16384>
    static cocls::generator<std::string_view> generator_coro(V v) {
        Serializer sr(v);
        std::array<char, max_buffer_size> buff;
        std::size_t cnt = sr.to_buffer(buff);
        while (cnt) {
            co_yield std::string_view(buff.data(), cnt);
            cnt = sr.to_buffer(buff);
        }
    }

    Stack _stack;
};






}

}



#endif /* SRC_COROSERVER_JSON_SERIALIZER_H_ */
