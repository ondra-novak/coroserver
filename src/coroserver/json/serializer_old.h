/*
 * serializer.h
 *
 *  Created on: 26. 1. 2023
 *      Author: ondra
 */

#ifndef SRC_USERVER_JSON_SERIALIZER_H_
#define SRC_USERVER_JSON_SERIALIZER_H_

#include "value.h"

#include <cmath>
#include <stack>


namespace userver {

namespace json {




class Serializer {
protected:
    enum State {
        node,
        object,
        array
    };

    struct Level {
        const Value &val;
        State state;
        std::variant<std::monostate, Object::const_iterator, Array::const_iterator> pos;
    };




    std::stack<Level> _levels;
    std::vector<char> _buff;
    const char *_buffiter = nullptr;
    const char *_buffend = nullptr;
public:

    Serializer(const Value &val) {
        _levels.push({
            val,node
        });
    }

    template<typename Iter>
    Iter serialize(Iter begin, Iter end) {

        while (begin != end) {

            if (_buffiter == _buffend) {
                if (serialize_step()) break;;
            } else {
                *begin = *_buffiter;
                ++begin;
                ++_buffiter;

            }

        }
        return begin;
    }

    template<typename Iter>
    void serialize(Iter back_ins) {

        do {
            std::copy(_buffiter, _buffend, back_ins);
            _buffiter = _buffend;
        } while(!serialize_step());

    }

    bool serialize_step() {

            if (_levels.empty()) return true;
            auto &curlev = _levels.top();
            switch (curlev.state) {
                case node:

                    switch (curlev.val.type()) {
                        case Type::object:
                            sendString("{");
                            curlev.state = object;
                            curlev.pos = curlev.val.get_object().begin();
                            break;
                        case Type::array:
                            sendString("[");
                            curlev.state = array;
                            curlev.pos = curlev.val.get_array().begin();
                            break;
                        default:
                            std::visit([&](const auto &x){
                                itemToBuffer(x);
                            }, curlev.val);
                            _levels.pop();
                    }

                    break;
                case object: {
                    const Object &obj = curlev.val.get_object();
                    auto &pos = std::get<Object::const_iterator>(curlev.pos);
                    if (pos != obj.end()) {
                        _buff.clear();
                        if (pos != obj.begin()) _buff.push_back(',');
                        appendString(pos->first);
                        _buff.push_back(':');
                        sendString();
                        const Value &item = pos->second;
                        ++pos;
                        _levels.push({item, node});
                    } else {
                        sendString("}");
                        _levels.pop();
                    }
                }
                break;
                case array: {
                    const Array &obj = curlev.val.get_array();
                    auto &pos = std::get<Array::const_iterator>(curlev.pos);
                    if (pos != obj.end()) {
                        if (pos != obj.begin()) sendString(",");
                        const Value &item = *pos;
                        ++pos;
                        _levels.push({item, node});
                    } else {
                        sendString("]");
                        _levels.pop();
                    }
                }
                break;
            }
            return false;
     }


protected:
    void itemToBuffer(std::int32_t x) {
        signedToBuffer(x);
    }
    void itemToBuffer(std::uint32_t x) {
        unsignedToBuffer(x);
    }
    void itemToBuffer(std::int64_t x) {
        signedToBuffer(x);
    }
    void itemToBuffer(std::uint64_t x)  {
        unsignedToBuffer(x);
    }
    void itemToBuffer(const Object &x)  {}
    void itemToBuffer(const Array &x)  {}
    void itemToBuffer(const PObject &x)  {}
    void itemToBuffer(const PArray &x)  {}

    void itemToBuffer(double x)  {
        if (!std::isfinite(x)) {
            if (std::isinf(x)) {
                if (x > 0) sendString("\"+∞\"");
                else sendString("\"-∞\"");
            } else {
                sendString("null");
            }
        } else {
            _buff.clear();
            if (x < 0) {
                _buff.push_back('-');
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
            if (v == 0) {
                _buff.push_back('0');
            } else {
                unsignedToBuffer2(v);
            }
            if (decpart > 0.000001) {
                _buff.push_back('.');
                int cnt = 15;
                while (cnt && decpart > 0.000001) {
                    decpart = std::modf(decpart*10,&intpart);
                    v = static_cast<unsigned int>(intpart);
                    if (decpart > 0.999999) {
                        ++v;
                        decpart = 0;
                    }
                    _buff.push_back(v+'0');
                    cnt--;
                }
            }
            if (dexp>0) {
                _buff.push_back('E');
                unsignedToBuffer2(static_cast<unsigned int>(dexp));
            } else if (dexp < 0) {
                _buff.push_back('E');
                _buff.push_back('-');
                unsignedToBuffer2(static_cast<unsigned int>(-dexp));
            }
            sendString();
        }
    }
    void sendString(const std::string_view &x) {
        _buffiter = x.data();
        _buffend = _buffiter+x.size();
    }
    void sendString() {
        _buffiter = _buff.data();
        _buffend = _buffiter+_buff.size();
    }
    void itemToBuffer(const TextNumber &x) {
        _buffiter = x.data();
        _buffend = _buffiter + x.size();
    }
    void itemToBuffer(bool x) {
        sendString(x?str_true:str_false);
    }
    void itemToBuffer(std::nullptr_t) {
        sendString(str_null);
    }
    void itemToBuffer(Undefined) {
        sendString(str_null);
    }
    template<typename T>
    void unsignedToBuffer(T x) {
        _buff.clear();
        if (x) {
            unsignedToBuffer2(x);
        } else {
            _buff.push_back('0');
        }
        sendString();
    }
    template<typename T>
    void unsignedToBuffer2(T x) {
        if (x) {
            unsignedToBuffer2(x/10);
            _buff.push_back('0'+(x%10));
        }
    }
    template<typename T>
    void signedToBuffer(T x) {
        if (x < 0) {
            _buff.clear();
            _buff.push_back('-');
            unsignedToBuffer2(-x);
        } else {
            unsignedToBuffer(x);
        }
    }
    void itemToBuffer(const std::string &x) {
        _buff.clear();
        appendString(x);
        sendString();
    }

    void appendString(const std::string_view &x) {
        int unicode = 0;
        _buff.push_back('"');
        for (char c : x) {
            if (c & 0x80) {
                if ((c & 0x40) == 0) {
                    unicode = (unicode << 6) | (c & 0x3F);
                } else {
                    flush_unicode(unicode);
                    if ((c & 0x20) == 0) unicode = (c & 0x1F);
                    else if ((c & 0x10) == 0) unicode = (c & 0x0F);
                    else if ((c & 0x08) == 0) unicode = (c & 0x07);
                    else if ((c & 0x04) == 0) unicode = (c & 0x03);
                    else if ((c & 0x02) == 0) unicode = (c & 0x01);
                }
            } else {
                if (unicode) flush_unicode(unicode);
                switch(c) {
                    case '"': _buff.push_back('\\');_buff.push_back('"');break;
                    case '\\': _buff.push_back('\\');_buff.push_back('\\');break;
                    case '/': _buff.push_back('\\');_buff.push_back('/');break;
                    case '\n': _buff.push_back('\\');_buff.push_back('n');break;
                    case '\r': _buff.push_back('\\');_buff.push_back('r');break;
                    case '\t': _buff.push_back('\\');_buff.push_back('t');break;
                    case '\f': _buff.push_back('\\');_buff.push_back('f');break;
                    case '\b': _buff.push_back('\\');_buff.push_back('b');break;
                    default: if (c >= '\0' && c < ' ') {
                        flush_unicode2(c);
                    } else {
                        _buff.push_back(c);
                    }
                }
            }
        }
        if (unicode) flush_unicode(unicode);
        _buff.push_back('"');
    }

    void flush_unicode(int c) {
        if (c > 0xFFFF) {
            int high_surrogate = (0xD800 | (c >> 10));
            int low_surrogate = (0xDC00 | (c & 0x3FF));
            flush_unicode2(high_surrogate);
            flush_unicode2(low_surrogate);
        } else {
            flush_unicode2(c);
        }
    }
    void flush_unicode2(int c) {
        _buff.push_back('\\');
        _buff.push_back('u');
        int u1 = (c >> 12) & 0xF;
        int u2 = (c >> 8) & 0xF;
        int u3 = (c >> 4) & 0xF;
        int u4 = (c     ) & 0xF;
        static char hextable[] = "0123456789abcdef";
        _buff.push_back(hextable[u1]);
        _buff.push_back(hextable[u2]);
        _buff.push_back(hextable[u3]);
        _buff.push_back(hextable[u4]);
    }
};

std::string Value::to_string() const {
    Serializer srl(*this);
    std::string res;
    srl.serialize(std::back_inserter(res));
    return res;
}


}

}



#endif /* SRC_USERVER_JSON_SERIALIZER_H_ */
