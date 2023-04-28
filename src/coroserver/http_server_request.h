/*
 * http_server_request.h
 *
 *  Created on: 16. 4. 2023
 *      Author: ondra
 */

#ifndef SRC_COROSERVER_HTTP_SERVER_REQUEST_H_
#define SRC_COROSERVER_HTTP_SERVER_REQUEST_H_

#include <cocls/common.h>
#include <cocls/future_conv.h>
#include <cocls/coro_storage.h>

#include "stream.h"

#include "http_common.h"


#include "coro_alloc.h"
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

    ~ServerRequest();

    ///load and parse the request
    /**
     * Reads input stream and loads all headers until empty line is reached. It
     * parses some headers and prepares stream to read http request body (if any)
     * @retval true request has been successfully parsed and accepted
     * @retval false request has been rejected. Use get_status() to retrieve status code.
     * If the status is 0, then request has been malformed, it is recommended to disconnect
     * the peer. If status is different from 0, it is possible to send error response with
     * this status. In all cases, keep_alive is disabled, you should disconnect the stream
     * right after response.
     */
    cocls::future<bool> load();

    ///retrieve method
    Method get_method() const {return _method;}
    ///retrive version
    Version get_version() const {return _version;}
    ///retrieve path
    std::string_view get_path() const {return _path;}
    ///retrieve host
    std::string_view get_host() const {return _host;}

    ///retrieve set status
    int get_status() const {return _status_code;}

    std::string_view get_status_message() const {return _status_message;}
    ///set status
    ServerRequest& set_status(int status);
    ///set status and status message
    ServerRequest& set_status(int code, const std::string_view &message);
    ///retrieve a header
    HeaderValue operator[](const std::string_view &key) const {
        return _req_headers[key];
    }
    ///retrieve all headers
    const HeaderMap &headers() const {
        return _req_headers;
    }

    ///add header
    void add_header(const std::string_view &key, const std::string_view &value);
    ///add header
    void add_header(const std::string_view &key, const std::size_t &value);
    ///add header
    void add_header(const std::string_view &key, const std::chrono::system_clock::time_point &value);
    ///set content type
    ServerRequest &content_type(ContentType ct);
    ///disable response buffering
    ServerRequest &no_buffering();
    ///set response caching
    ServerRequest &caching(std::size_t seconds);
    ///set location header
    ServerRequest &location(const std::string_view &location);
    ///set last modified time
    ServerRequest &last_modified(std::chrono::system_clock::time_point tp);
    ///set last modified time
    ServerRequest &add_date(std::chrono::system_clock::time_point tp);
    ///add header
    ServerRequest &operator()(const std::string_view &key, const std::string_view &value);
    ///add header
    ServerRequest &operator()(const std::string_view &key, const std::size_t &value);
    ///add header
    ServerRequest &operator()(const std::string_view &key, const std::chrono::system_clock::time_point &value);

    ///check method
    bool is_method(Method m) const {return m == _method;}
    ///check supported method
    /**
     * @param methods supported methods
     * @retval true method supported
     * @retval false method not supported, function sets status 405 and sets Allow
     * header. It is expected that request ends with error response
     */
    bool allow(const std::initializer_list<Method> &methods);
    ///calculates absolute url
    /**
     * @return whole url calculated from headers
     *
     * @note To real url depends on how proxy is set up. So the result
     * could be unusable if there is some sort of url overwrite.
     *
     * The coroserver is written to minimalize need of overwrite
     * on the proxy, so it is recommended to pass requests without
     * a change.
     *
     * It is also required to pass properly Forwarded header to
     * determine whether connection is secure or not. The proxy
     * should also pass correct Host (not Forwarded: host=, which
     * is ignored).
     *
     */
    std::string_view get_url() const;

    ///returns true, if the headers has been already sent
    /**
     * @retval false headers was not sent
     * @retval true headers was send, it is impossible to change status or set any headers
     */
    bool headers_sent();

    ///returns true, whether keep alive is available
    /**
     * @retval true keep alive is active, you can load request again
     * @retval false connection was closed
     */
    bool keep_alive() const {
        return _keep_alive;
    }

    static std::string url_decode(const std::string_view &str);

    static void url_decode(const std::string_view &str, std::string &out);


    ///Determines, whether request has been touched
    /**
     * @retval true request has not been touched, so it can be still processed complete.
     * This state requires _body_processed == false and _headers_sent == false and _status_code == 0.
     * @retval false request has been touched, so it cannot be processed complete.
     */
    bool untouched() const {
        return !_body_processed && !_headers_sent && _status_code == 0;
    }


    ///Determines whether headers has been already sent, so further changes to headers has no effect
    bool headers_sent() const {
        return _headers_sent;
    }

    auto get_counters() const {
        return _cur_stream.get_counters();
    }

    ///clear all output headers
    void clear_headers();

    void content_type_from_extension(const std::string_view &path);
    ///Retrieve body stream
    /**
     * @return stream. Returns empty stream if request has no body (however it still allocates
     * an empty stream).
     *
     * You should avoid to call function by multiple times
     */
    cocls::future<Stream> get_body();
    ///Sends response (status code is set to 200 in case, that status code is not set)
    /**
     * @param body body to send as string. Note that underlying string must remain
     * valid until the function is complete. Otherwise, convert content to string in
     * the argument
     *
     *
     * @return future for completion
     *
     * @code
     * cocls::asyn<void> coro(ServerRequest &req) {
     *      std::string_view text("text to send");
     *      co_await req.send(text);
     * }
     *
     * cocls::future<void> not_coro(ServerRequest &req) {
     *      std::string_view text("text to send");
     *      return req.send(std::string(text));
     * }
     * @endcode
     *
     */
    cocls::future<bool> send(std::string_view body);
    ///Send response and retrieve stream to send response body
    cocls::future<Stream> send();
    ///Send response prepared in content of std::ostringstream
    /**
     * @param body in stringstream. Function moves the content to the internal buffer
     * and then calls send(<string>), so the reference to parameter don't need to be kept
     * until the completion
     * @return
     */
    cocls::future<bool> send(std::ostringstream &body);

    ///Send the string
    /**
     * @param body string to send, must be passed as rvalue reference
     * The string is stored in a internal buffer, so you don't
     * need to wait for completion
     * @return
     *
     * @code
     * cocls::future<void> not_coro(ServerRequest &req) {
     *      std::string_view text("text to send");
     *      return req.send(std::string(text));
     * }
     * @endcode
     */
    cocls::future<bool> send(std::string &&body) {
        _user_buffer = std::move(body);
        return send(std::string_view(_user_buffer));
    }

    ///Send C-like string
    /**
     * @param x string to send. Note that C-like strings are always
     * considered as statically allocated. For dynamically allocated
     * strings you need to use std::string.
     * For this purpose, the pointer and content where pointer
     * points must remain valid until function completes
     *
     * @return
     */
    cocls::future<bool> send(const char *x) {
        return send(std::string_view(x));
    }

    ///Send file
    /**
     * @param path pathname to file to send
     * @param use_chunked set true to use chunked format (otherwise it is used content-length)
     * @note it is possible to set headers before. You should set
     * Content-Type, catching, last-modified, etc
     * @return a future
     */
    cocls::future<bool> send_file(const std::string &path, bool use_chunked = false);

    template<typename _IOStream, std::size_t buffer = 16384>
    cocls::future<bool> send_stream(_IOStream stream)  {
        Stream s = co_await send();
        char buff[buffer];
        while (!stream.eof()) {
            stream.read(buff, sizeof(buff));
            std::size_t sz = stream.gcount();
            if (sz) {
                bool b = co_await s.write(std::string_view(buff,sz));
                if (!b) co_return false;
            } else {
                break;
            }
        }
        co_await s.write_eof();
        co_return true;

    }

    ///Contains name of server (passed to the response)
    static std::string server_name;


    ///a user bufer
    /** You can store anything there, however, some function
     * can overwrite it (send() functions)
     */
    std::string _user_buffer;
protected:

    struct OutputHdrs {
        bool _has_ctxtp;
        bool _has_ctlen;
        bool _has_te;
        bool _has_te_chunked;
        bool _has_connection;
        bool _has_connection_upgrade;
        bool _has_connection_close;
        bool _has_date;
        bool _has_server;
        std::size_t _ctlen;
    };


    Stream _cur_stream;

    bool parse_request(std::string_view req_header);
    bool parse_headers();

    std::vector<char> _header_data;
    std::vector<char> _output_headers;
    OutputHdrs _output_headers_summary;
    HeaderMap _req_headers;

    int _status_code = 0;
    Method _method = Method::unknown;
    Version _version = Version::unknown;
    mutable std::string _url_cache;
    std::string_view _path;
    std::string_view _vpath;
    std::string_view _host;
    std::string_view _status_message;
    bool _keep_alive = false;
    bool _expect_100_continue = false;
    bool _has_body = false;
    bool _body_processed = false;
    bool _headers_sent = false;

    Stream _body_stream;


    cocls::suspend_point<void> load_coro(std::string_view &data, cocls::promise<bool> &res);
    cocls::future_conv<&ServerRequest::load_coro> _load_awt;
    std::size_t _search_hdr_state = 0;

    Stream get_body_coro(bool &res);
    cocls::future_conv<&ServerRequest::get_body_coro> _get_body_awt;


    cocls::future<bool> discard_body_intr();

    cocls::suspend_point<void> discard_body_coro(std::string_view &data, cocls::promise<bool> &res);
    cocls::future_conv<&ServerRequest::discard_body_coro> _discard_body_awt;

    cocls::suspend_point<void> send_resp(bool &, cocls::promise<Stream> &res);
    cocls::future_conv<&ServerRequest::send_resp> _send_resp_awt;

    cocls::suspend_point<void> send_resp_body(Stream &s, cocls::promise<bool> &res);
    cocls::future_conv<&ServerRequest::send_resp_body> _send_resp_body_awt;
    std::string_view _send_body_data;

    static bool future_forward(bool &b) {return b;}
    cocls::future_conv<future_forward> _forward_awt;



    std::string_view prepare_output_headers();





};

}
}




#endif /* SRC_COROSERVER_HTTP_SERVER_REQUEST_H_ */
