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
     * @param secure set true if connection is considered secure, which enforces 'https'.
     * Otherwise, function can detect, whether connection is secure.
     * If the connection is websocket, it uses 'ws' and 'wss'
     * @return
     */
    std::string get_url(bool secure) const;

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

    ///clear all output headers
    void clear_headers();

    void content_type_from_extension(const std::string_view &path);

    ///discard body (if necessery)
    /**
     * Ensures, that request body is discarded. If request has no body, function immediately
     * returns. If request has body, but 100-continue is expected, the function also returns
     * immediately, as it is possible to send response without reading the body
     * @return
     */
    cocls::future<void> discard_body();

    ///Retrieve body stream
    /**
     * @return stream. Returns empty stream if request has no body (however it still allocates
     * an empty stream).
     *
     * You should avoid to call function by multiple times
     */
    cocls::future<Stream> get_body();
    ///Sends response (status code is set to 200 in case, that status code is not set)
    cocls::future<void> send(std::string_view body);
    ///Sends response (status code is set to 200 in case, that status code is not set)
    cocls::future<void> send(ContentType ct, std::string_view body);
    ///Send response and retrieve stream to send response body
    cocls::future<Stream> send();

    cocls::future<bool> send_file(const std::string &path, bool use_chunked = false);

    static std::string server_name;

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

    stackful_storage _coro_storage;

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
    std::string_view _path;
    std::string_view _host;
    std::string_view _status_message;
    bool _keep_alive = false;
    bool _expect_100_continue = false;
    bool _has_body = false;
    bool _body_processed = false;
    bool _headers_sent = false;
    std::string _response_line;

    Stream _body_stream;



    template<typename Alloc>
    cocls::with_allocator<Alloc, cocls::async<Stream> > get_body_coro(Alloc &);
    template<typename Alloc>
    cocls::with_allocator<Alloc, cocls::async<Stream> > send_coro(Alloc &);
    template<typename Alloc>
    cocls::with_allocator<Alloc, cocls::async<void> > send_coro(Alloc &, std::string_view body);
    template<typename Alloc>
    cocls::with_allocator<Alloc, cocls::async<void> > discard_body_coro(Alloc &);
    template<typename Alloc>
    cocls::with_allocator<Alloc, cocls::async<bool> > load_coro(Alloc &);

    std::string_view prepare_output_headers();





};

}
}




#endif /* SRC_COROSERVER_HTTP_SERVER_REQUEST_H_ */
