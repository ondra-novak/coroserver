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

    inline void flush_unicode(std::string &buff, int &c) {
        if (c > 0xFFFF) {
            int high_surrogate = (0xD800 | (c >> 10));
            int low_surrogate = (0xDC00 | (c & 0x3FF));
            flush_unicode2(buff, high_surrogate);
            flush_unicode2(buff, low_surrogate);
        } else {
            flush_unicode2(buff, c);
        }
        c = 0;
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


template<typename Alloc, typename Stream>
inline cocls::with_allocator<Alloc, cocls::async<bool> >serialize_coro(Alloc &,Stream stream, const Value &v) {


    struct ObjectRange {
        Object::const_iterator pos;
        Object::const_iterator end;
        bool value;
    };
    struct ArrayRange {
        Array::const_iterator pos;
        Array::const_iterator end;
    };
    using StackItem = std::variant<ObjectRange,ArrayRange>;
    std::stack<StackItem> stack;
    CharacterWriter wr(stream);
    Value tmp;
    std::string buff;
    std::string_view output;

    const Value *cobj = &v;
    while (cobj) {
        switch (cobj->type()) {
            case Type::array: {
                const Array &a = cobj->get_array();
                if (!co_await wr('[')) co_return false;
                if (a.empty()) {
                    if (!co_await wr(']')) co_return false;
                } else {
                    auto iter = a.begin();
                    cobj = &(*iter);
                    ++iter;
                    stack.push(ArrayRange{iter, a.end()});
                    continue;
                }
            }break;
            case Type::object: {
                const Object &o = cobj->get_object();
                if (!co_await wr('{')) co_return false;
                if (o.empty()) {
                    if (!co_await wr(']')) co_return false;
                } else {
                    auto iter = o.begin();
                    tmp = iter->first;
                    cobj = &tmp;
                    stack.push(ObjectRange{iter, o.end(), true});
                    continue;

                }
            }break;
            case Type::null  :
                               output = "null";
                               break;
            case Type::boolean:
                               output = cobj->get_bool()?"true":"false";
                               break;
            case Type::number:
            case Type::string: {
                buff.clear();
                std::visit([&](const auto &x){
                    using Type = std::decay_t<decltype(x)>;
                    if constexpr(std::is_same_v<Type,std::int32_t> || std::is_same_v<Type, std::int64_t>) {
                        _details::signed_to_string(buff, x);
                    } else if constexpr(std::is_same_v<Type,std::uint32_t> || std::is_same_v<Type, std::uint64_t>) {
                        _details::unsigned_to_string(buff, x);
                    } else if constexpr(std::is_same_v<Type,double>) {
                        _details::double_to_string(buff, x);
                    } else if constexpr(std::is_same_v<Type,TextNumber>) {
                        buff.append(x);
                    } else if constexpr(std::is_same_v<Type,std::string>) {
                        _details::string_to_string(buff, x);
                    } else {
                        buff = "\"?\"";
                    }

                },*cobj);
                output = buff;
            } break;
            default:
            case Type::undefined:
                               output = "\"undefined\"";
           break;
        }

        cobj = nullptr;

        for (char c: output) {
            if (!co_await wr(c)) co_return false;
        }
        output = {};


        while (!stack.empty()) {
            StackItem &t = stack.top();
            if (std::holds_alternative<ArrayRange>(t)) {
                ArrayRange &r = std::get<ArrayRange>(t);
                if (r.pos == r.end) {
                    if (!co_await wr(']')) co_return false;
                    stack.pop();
                } else {
                    cobj = &(*r.pos);
                    ++r.pos;
                    if (!co_await wr(',')) co_return false;
                    break;
                }
            } else if (std::holds_alternative<ObjectRange>(t)) {
                ObjectRange &r = std::get<ObjectRange>(t);
                if (r.pos == r.end) {
                    if (!co_await wr('}')) co_return false;
                    stack.pop();
                } else if (r.value) {
                    cobj = &r.pos->second;
                    ++r.pos;
                    r.value = false;
                    if (!co_await wr(':')) co_return false;
                    break;
                } else {
                    tmp = r.pos->first;
                    r.value = true;
                    cobj = &tmp;
                    if (!co_await wr(',')) co_return false;
                    break;
                }
            }
        }
    }
    co_return co_await wr.flush();
}

template<typename Stream>
cocls::future<bool> serialize(Stream stream, const Value &v) {
    cocls::default_storage stor;
    return serialize_coro(stor, stream, v);

}




}

}



#endif /* SRC_COROSERVER_JSON_SERIALIZER_H_ */
