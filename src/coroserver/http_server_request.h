/*
 * http_server_request.h
 *
 *  Created on: 16. 4. 2023
 *      Author: ondra
 */

#ifndef SRC_COROSERVER_HTTP_SERVER_REQUEST_H_
#define SRC_COROSERVER_HTTP_SERVER_REQUEST_H_

#include <cocls/common.h>
#include <cocls/future.h>
#include <cocls/coro_storage.h>

#include "stream.h"

#include "http_common.h"

namespace coroserver {

namespace http {

class BodyData: public std::vector<char> {
public:
    using std::vector<char>::vector;
    operator std::string_view() const {return std::string_view(data(),size());}
};


class ServerRequest {
public:
    ServerRequest(Stream s);

    ///load and parse the request
    cocls::future<bool> load();


    Method get_method() const {return _method;}
    Version get_version() const {return _version;}
    std::string_view get_path() const {return _path;}
    std::string_view get_host() const {return _host;}

    int get_status() const {return _status_code;}
    void set_status(int status) {_status_code = status;}
    void set_status(int code, const std::string_view &message);
    HeaderValue operator[](const std::string_view &key) const {
        return _req_headers[key];
    }
    const HeaderMap &headers() const {
        return _req_headers;
    }

    void add_header(const std::string_view &key, const std::string_view &value);
    void add_header(const std::string_view &key, const std::size_t &value);
    void set_content_type(ContentType ct);
    void set_no_buffering();
    void set_caching(std::size_t seconds);
    void set_location(const std::string_view &location);
    void set_last_modified(std::chrono::system_clock::time_point tp);

    bool is_method(Method m) const {return m == _method;}
    bool allow(const std::initializer_list<Method> &methods);
    std::string get_url(std::string_view protocol) const;

    cocls::future<bool> send(std::string_view body);
    cocls::future<bool> send(ContentType ct, std::string_view body);


protected:

    cocls::reusable_storage _coro_storage;

    Stream _cur_stream;

    cocls::suspend_point<void> parse_request(cocls::future<bool> &f) noexcept;
    bool parse_headers();
    cocls::promise<bool> _load_promise;
    cocls::call_fn_future_awaiter<bool, ServerRequest, &ServerRequest::parse_request> _load_awt;
    std::string _header_data;
    HeaderMap _req_headers;

    int _status_code = 0;
    Method _method;
    Version _version;
    std::string_view _path;
    std::string_view _host;
    bool _keep_alive;
    bool _expect_100_continue;
    bool _has_body;

    Stream _body_stream;


};

}
}




#endif /* SRC_COROSERVER_HTTP_SERVER_REQUEST_H_ */
