#pragma once
#include "stream.hpp"
#include "http_common.h"
#include <vector>

namespace coroserver {


class HttpServerRequest {
public:

    static constexpr std::size_t max_header_size = 65536;
    static constexpr std::string_view header_separator = "\r\n\r\n";


    HttpServerRequest(Stream s);

    ///load header from network
    coro::awaitable<bool> load();

protected:

    Stream _s;
    std::vector<char> _recv_header_data;
    std::vector<std::pair<HeaderKey, HeaderValue> > _recv_header;
    HeaderKey _method;
    HeaderKey _protocol;
    std::string_view _path;

    coro::awaiting_callback<coro::awaitable<ReceiveBlockStatus>, 
                HttpServerRequest *, coro::awaitable<bool>::result> _read_until_callback;
    coro::awaiting_callback<coro::awaitable<bool>, 
                HttpServerRequest *, coro::awaitable<bool>::result> _send_callback;


    void reset();

    enum class ParseHeaderStatus {
        //request accepted, you can process result
        ok,
        //request parse error, close connection
        parse_error,
        //request syntaxtically ok, but rejected because headers or values
        /**
         * http response should be generated
         * load should be repeated
         */
        rejected
    };

    ParseHeaderStatus parse_header();

};


}