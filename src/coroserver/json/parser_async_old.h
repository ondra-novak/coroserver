#pragma once
#ifndef SRC_USERVER_JSON_PARSER_ASYNC_H_
#define SRC_USERVER_JSON_PARSER_ASYNC_H_

#include "parser_old.h"

#include "../async.h"



namespace userver {

namespace json {

class ParseError: public std::exception {
public:
    virtual const char *what() const noexcept override {
        return "JSON parse error";
    }

};


namespace _details {

template<typename Source, typename PutBackFn>
future<Value> parse_internal(Source source, PutBackFn fn) {
    std::string_view put_back;
    Parser p;
    do {
        std::string_view str = co_await source();
        put_back = p.parse_string(str);
    } while (p.more());
    fn(put_back);
    if (p.is_error()) throw ParseError();
    co_return std::move(p.get_result());
}

}

template<typename Source>
future<Value> parse(Source &&source) {
    return _details::parse_internal(std::forward<Source>(source), [](auto &){});
}
template<typename Source>
future<Value> parse(Source &&source, std::string_view &putback) {
    return _details::parse_internal(std::forward<Source>(source), [&](auto &x){putback = x;});
}
template<typename Stream>
future<Value> parse_stream(Stream stream) {
    return _details::parse_internal([=]() mutable {
        return stream.read();
    }, [=](auto &x) mutable {stream.put_back(x);});
}


}

}

#endif /* SRC_USERVER_JSON_PARSER_ASYNC_H_ */
