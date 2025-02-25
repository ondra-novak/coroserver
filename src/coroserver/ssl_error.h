#pragma once

#include <exception>
#include <string>
#include <openssl/err.h>

class SSLException : public std::exception {
private:
    unsigned long errorCode;  // OpenSSL kód chyby
    std::string errorMessage; // OpenSSL chybová zpráva

public:
    SSLException() {
        errorCode = ERR_get_error();
        char buffer[256];
        ERR_error_string_n(errorCode, buffer, sizeof(buffer));
        errorMessage = buffer;
    }

    explicit SSLException(const std::string& customMessage) {
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


