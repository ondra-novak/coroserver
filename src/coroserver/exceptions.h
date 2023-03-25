#pragma once
#ifndef _SRC_USERVER20_EXCEPTIONS_
#define _SRC_USERVER20_EXCEPTIONS_
#include <exception>





namespace userver {


class NetworkException: public std::exception {
    
};

class ConnectFailedException: public NetworkException {
public:
    const char *what() const noexcept override {
        return "Connect failed";
    }
};

class TimeoutException: public NetworkException {
public:
    const char *what() const noexcept override {
        return "Timeout";
    }
};

class PendingOperationCanceled: public NetworkException {
public:
    const char *what() const noexcept override {
        return "Pending operation canceled";
    }
};

class NoMoreData: public NetworkException {
    const char *what() const noexcept override {
        return "No more data can be read from the stream (EOF)";
    }
};

class InvalidChunkedStream: public NetworkException {
public:
    const char *what() const noexcept override {
        return "Invalid chunked stream";
    }
    
};

///throws by HttpServerRequest when body is not complete processed
/**
 * If you reading body, and you did not read it complete, exception is thrown
 * If you write body and you did not write body complete, exception is thrown
 * 
 * For chunked stream this can happen when you stop reading before the end chunk is 
 * reached. 
 * 
 * For limited stream this can happen when you read or write less bytes then the stream has
 * allocated.
 * 
 * To avoid this exception, always read until eof, and always write advertised count of bytes.
 * You can call write_eof() to speed this process 
 * 
 */
class IncompleteBody: public NetworkException {
public:
    const char *what() const noexcept override {
        return "Incomplete body";
    }
};

///Connection has been reset before certain operation could be finished
class ConnectionReset: public NetworkException {
public:
    const char *what() const noexcept override {
        return "Connection reset";
    }
};


}

#endif
