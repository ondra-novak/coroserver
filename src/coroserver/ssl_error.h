#pragma once

#include <exception>
#include <string>
#include <openssl/err.h>

class SSLException: public std::exception {
public:
};

class SSLError : public SSLException {
private:
    unsigned long errorCode;  // OpenSSL kód chyby
    std::string errorMessage; // OpenSSL chybová zpráva

public:
    SSLError() {
        errorCode = ERR_get_error();
        char buffer[256];
        ERR_error_string_n(errorCode, buffer, sizeof(buffer));
        errorMessage = buffer;
    }

    explicit SSLError(const std::string& customMessage) {
        errorCode = ERR_get_error();
        char buffer[256];
        ERR_error_string_n(errorCode, buffer, sizeof(buffer));
        errorMessage = customMessage + " | OpenSSL error: " + buffer;
    }

    const char* what() const noexcept override {
        return errorMessage.c_str();
    }

    unsigned long getErrorCode() const {
        return errorCode;
    }

    std::string getErrorMessage() const {
        return errorMessage;
    }
};


class SSLVerificationException : public SSLException {
public:
    SSLVerificationException(long error_code, std::string message)
        :_error_code(error_code)
        ,_msg(std::move(message)) {}

    long get_error_code() const { return _error_code; }

    const std::string &get_message() const {return _msg;}

    const char* what() const noexcept override {
        return _msg.c_str();
    }

private:
    long _error_code;
    std::string _msg;
};

