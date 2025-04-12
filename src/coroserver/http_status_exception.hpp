#pragma once

#include "stream.hpp"
#include "http_common.hpp"
#include "null_stream.h"
#include <stdexcept>
namespace coroserver {

namespace http {

///Exception can carry status code of failed HTTP request
class StatusException : public std::exception {
public:


    ///Create exception
    /**
     * @param code status code
     * @param message status message
     */
    StatusException(unsigned int code, std::string message)
        :_code(code),_message(std::move(message))
        ,_ct(ContentType::octet_stream), _body(NullStream::create()) {}

    ///Create exception
    /**
     * @param code status code
     * @param message status message
     * @param ct content type
     * @param body stream containing body, can carry additional informations
     */
    StatusException(unsigned int code, std::string message,
            ContentType ct, Stream &&body)
        :_code(code),_message(std::move(message))
        ,_ct(ct),_body(std::move(body)) {}


    ///Retrieve body
    /**
     * @return body stream. If the stream was not associated, returns NullStream
     */
    Stream get_body() const {
        return _body;
    }

    ///Get status code
    unsigned int get_code() const {
        return _code;
    }

    ///Get content type
    ContentType get_content_type() const {
        return _ct;
    }


    ///Get status message
    const std::string& get_message() const {
        return _message;
    }

    ///Get what message
    virtual const char *what() const noexcept override {
        if (_buff.empty()) {
            _buff = "Unexpect HTTP status: " + std::to_string(_code)+" " + _message;
        }
        return _buff.c_str();
    }
protected:
    unsigned int _code;
    std::string _message;
    ContentType _ct;
    Stream _body;
    mutable std::string _buff;
};



}

}
