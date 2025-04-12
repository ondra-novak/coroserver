#pragma once
#include "stream.hpp"
#include "http_common.hpp"

#include <filesystem>
#include <vector>
#include <sstream>

namespace coroserver {

namespace http {

class ServerRequest {
public:

    enum State {
        ///Request has been left untouched
        /** The handler is probably unable to process the request and
         * rejected it. The request can be passed to different handler
         */
        untouched,
        ///The body has been read, so request is partially processed
        /**
         * The request cannot be passed to different handler, but the
         * response still can be generated and sent, for example error
         * response
         */
        body_read,
        ///Headers has been prepared, but not sent yet
        /**
         * This means, that response can be changed. Handle can prepare
         * headers, set status and leave the request, the server can
         * include default response depend on status code
         */
        headers_prepared,
        ///Headers has been sent
        /**
         * The request is considered processed. In case of
         * exception, the request must be discarded (no keep alive)
         */
        headers_sent
    };

    ///Create server request
    /**
     * @param s connected stream, right connected, or TLS session established
     * @param server_name name of server. This is optional and specified
     * content of Server header. The string must be allocated while
     * this request exists, the best way to allocate name statically
     * @param con_type specify connection type
     */
    ServerRequest(Stream s, std::string_view server_name = {}, ConnectionType con_type = ConnectionType::direct_unsecure);

    ///maximum size of header
    static constexpr std::size_t max_header_size = 65536;
    ///maximum size of response status line
    /**
     * The response status line consists of status code and message.
     * The maximum size of the status line is 100 bytes. This should cover all possible status codes and messages.
     */
    static constexpr std::size_t status_line_reservation = 100;

    ///header separator
    static constexpr std::string_view header_separator = "\r\n\r\n";

    ///contains name of server
    static std::string_view default_server_name;


    ///Parse request
    /**
     * @param s source stream
     * @return awaitable returns true, if headers are valid, or
     * false if parse failed
     */
    awaitable<bool> parse();


    ///Returns true, if there is body to read
    /**
     * The body should be read. If the request is left without reading the
     * body, the keep alive is ignored and request is closed
     * @retval true there is body
     * @retval false no body
     *
     * @note the function get_body() resets this flag
     */
    bool has_body() const {return _has_body;}

    ///Reads body
    /**
     * @return returns Stream with body. If there is no body, returns NullStream
     * If the expect-100, this function send 100 Continue
     */
    awaitable<Stream> get_body();


    /// Rerieves header value as unsigned int
    /**
     * @param key header key
     * @return returns value of header as unsigned int, or empty if not found
     */

    std::optional<std::size_t> get_header_uint(HeaderKey key) const;

    /// Retrieves content length from header
    /**
     * @return returns content length, or empty if not found
     */
    std::optional<std::size_t> get_content_length() const;

    /// Retrieves header value
    /**
     * @param key header key
     * @return returns value of header, or empty if not found
     */
    std::optional<std::string_view> get_header(HeaderKey key) const;

    /// Retrieves stream associated with the request
    /**
     * @return returns stream associated with the request
     */
    Stream get_source_stream() const;

    /// Retrieves whether the request is keep alive
    /**
     * @return returns true, if the request is keep alive, or false if not
     * @note The connection can stay keep alive if this is enabled,
     *  the connection was not upgraded and body was processed or was not present
     */
    bool is_keep_alive() const {return _keep_alive && !_has_body && !_upgrade;}

    ///Retrieve method type
    /**
     * @return returns method type
     */
    Method get_method() const {return _method;}

    /// Retrieve URI path
    /**
     * @return returns URI path
     * @note The path is retrieved directly from the request without normalization.
     */
    const std::string_view& get_path() const {return _path;}

    /// Retrieve HTTP protocol version
    /**
     * @return returns HTTP protocol version
     */
    Protocol get_protocol() const {return _protocol;}

    ///Retrieve content type if it is known
    ContentType get_content_type() const;

    /// Retrieve all received headers as key-value pairs
    /**
     * @return returns all received headers as key-value pairs
     * @note The key is case insensitive, so the header "Content-Type" is the same as "content-type"
     */
    const std::vector<std::pair<HeaderKey, std::string_view> >& get_recv_header() const {return _recv_header;}

    /// Retrieve currently set status code for response
    /**
     * @return returns currently set status code for response
     * @note The status code is set by set_status() function. The default value is 200 OK
     */
    unsigned int get_status() const {return _status;}

    /// Retrieve whether client expects 100 continue
    /**
     * @return returns true, if client expects 100 continue, or false if not
     * @note The function is set by header Expect: 100-continue. The default value is false
     */
    bool is_expect_100() const {return _expect_100;}

    /// Retrieve status message for response
    /**
     * @return returns status message for response
     * @note The status message is set by set_status() function. The default value is "OK"
     */
    const std::string_view& get_status_message() const {return _status_message;}

    /// Retrieve whether the connection is upgraded
    /**
     * @return returns true, if the connection is upgraded, or false if not
     * @note The function is set by header Connection: upgrade. The default value is false
     */
    bool is_upgrade() const {return _upgrade;}
    /// Retrieve whether the connection is secure
    /**
     * @return returns true, if the connection is secure, or false if not
     * @note Return value depends on ConnectionType value. If the ConnectionType is reverse_proxy,
     * it checks the header X-Forwarded-Proto. If the ConnectionType is direct_unsecure, it returns false.
     */
    bool is_secure() const;

    /// Retrieve host name from header]
    /**
     * @return returns host name from header, or empty if not found
     * @note The function is set by header Host. The default value is empty
     */
    std::string_view get_host() const;
    ///Retrieve prefix for mapping paths
    /**
     * @return returns prefix for mapping paths, or empty if not found
     * @note The function is set by header X-Forwarded-Prefix. The default value is empty
     */
    std::string_view get_path_prefix() const;

    ///Set response header
    /**
     * @param key header key
     * @param value header value
     * @note there is no header sanitization, so the caller must ensure that the header is valid
     */
    void set_header(HeaderKey key, std::string_view value);
    ///Set response header as size_t value
    /**
     * @param key header key
     * @param sz header value as size_t
     * @note there is no header sanitization, so the caller must ensure that the header is valid
     */
    void set_header(HeaderKey key, std::size_t sz);
    ///Set response header as rfc5322 date
    /**
     * @param key header key
     * @param tm date as std::time_t value
     * @note there is no header sanitization, so the caller must ensure that the header is valid
     */
    void set_header_date_rfc5322(HeaderKey key, std::time_t tm);

    ///Set response status code
    /**
     * @param code status code
     * @note for known status codes, the message is set automatically
     */
    void set_status(unsigned int code);
    ///Set response status code and message
    /**
     * @param code status code
     * @param message status message
     */
    void set_status(unsigned int code, std::string_view message);
    ///Set content type
    /**
     * @param ctx content type
     * @note the function sets Content-Type header. The default value is text/plain;charset=utf-8
     */
    void set_content_type(ContentType ctx);
    ///Set response content length
    /**
     * @param len content length
     * @note the function sets Content-Length header. If the header is not set, the Transfer-Encoding can be set
     * to chunked. If nether header is set, the response is set to Connection: close and body ends with EOF.
     */
    void set_content_length(std::size_t len);

    /// Enables transfer encoding chunked
    void set_transfer_encoding_chunked();

    ///send response and body
    /**
     * @param response_body body of the response
     * @return awaitable
     * @retval true success
     * @retval false failure
     **/
    awaitable<bool> send(std::string_view response_body);
    ///send response and prepare stream for body
    /**
     * @return awaitable, which carries stream for body. If the send fails, return NullStream, which fails to write a body
     */
    awaitable<Stream> send();

    ///send output string stream
    /**
     * @param stream stream instance
     * @return awaitable
     * @retval true success
     * @retval false failure
     *
     * @note main benefit of this function is that content of stream is
     * associated with the request, so caller don't need to wait on the
     * result, it can pass result to the parent caller.
     */
    awaitable<bool> send(std::ostringstream &stream);

    ///Send file as response
    /**
     * @param p path to file
     * @return awaitable
     * @retval true success
     * @retval false failure
     *
     * @note The function sets Content-Type header based on file extension.
     */
    awaitable<bool> send_file(const std::filesystem::path &p);

    ///Send file as response with content type
    /**
     * @param p path to file
     * @param ctx content type
     * @return awaitable
     * @retval true success
     * @retval false failure
     */
    awaitable<bool> send_file(const std::filesystem::path &p, ContentType ctx);

    ///Send erro page
    /**
     * You need to set status by function set_status(). You can also
     * set additional headers (for example Allow for status 405)
     */
    awaitable<bool> send_error();

    ///Send error page with status code
    /**
     * @param status status code
     * @return awaitable
     * @retval true success
     * @retval false failure
     *
     * @note The function sets status code and message. It is shortcut for
     * set_status() and send_error().
     **/
    awaitable<bool> send_error(unsigned int status) {
        set_status(status);
        return send_error();
    }

    ///change response protocol type
    /**
     * @param protocol protocol type
     * @note The function sets the protocol type. The default value is protocol value from the request.
     */
    void set_protocol(Protocol protocol) {_protocol = protocol;}

    ///Retrieves current state of the request
    /**
     * This allows to determine whether the request is still untouched, or if the headers have been sent.
     * @return returns current state of the request
     **/
    State get_state() const;

    ///Send redirect response
    /**
     * @param uri URI to redirect to
     * @param type redirect type
     * @return awaitable
     * @retval true success
     * @retval false failure
     *
     * @note The function sets the status code and Location header. The default value is 302 Found.
     */
    awaitable<bool> redirect(std::string_view uri,RedirectType type = RedirectType::temporary);

    ///Retrieve request's absolute URL
    /**
     * @return returns request's absolute URL
     * @note The function retrieves the URL from the request. The URL is constructed based on the connection type and headers.
     * If the connection type is direct_unsecure, it uses http://. If the connection type is direct_secure, it uses https://.
     * If the connection type is reverse_proxy, it uses X-Forwarded-Proto header to determine the protocol.
     */
    std::string get_url() const;

    ///Assumes that current path is directory and redirects to it adding trailing slash
    /**
     * @param type redirect type
     * @retval true redirect is prepared, the caller must use send("") to send the prepared response.
     * @retval false redurect is not required, there is already a trailing slash
     *
     * @code {c++}
     * auto f = map_uri_to_path(base_path, req.get_path());
     * if (std::filesystem::is_directory(f) && req.redirect_to_directory()) {
     *      return req.send("");
     * }
     * @endcode
     *
     *
     */
    bool redirect_to_directory(RedirectType type = RedirectType::permanent);

    ///Filters methods.
    /**
     * @param methods list of methods to filter
     * @return return method if it is in the list, or unknown if not
     * @note The function sets status 405 Method Not Allowed if the method is not in the list. It also
     * sets Allow header with the list of allowed methods.
     *
     * @code {c++}
     * switch (req.filter_methods({Method::GET, Method::POST})) {
     *     case Method::GET: return process_get(req);
     *     case Method::POST: return process_post(req);
     *     default: return req.send_error();
     * }
     * @endcode
     *
     */
    Method filter_methods(std::initializer_list<Method> methods);

protected:

    ///Retrieve request's stream
    Stream _cur_stream;
    ///stores Server: name
    std::string_view _server_name;
    /// All headers received from the client
    std::vector<char> _recv_header_data;
    /// All headers received from the client as key-value pairs
    std::vector<std::pair<HeaderKey, std::string_view> > _recv_header;
    /// Connection type
    ConnectionType _con_type = {};
    /// Method of request
    Method _method = {};
    /// Protocol type
    Protocol _protocol = {};
    /// Path from request
    std::string_view _path = {};
    /// Status code of future response
    unsigned int _status = 0;
    /// Status message of future response
    std::string_view _status_message = {};
    /// Contains true, if the keep-alive is allowed
    bool _keep_alive = true; //start with true to unblock load
    /// Contains true, if 100-continue is expected
    bool _expect_100 = false;
    /// Contains true, if request has a body. This flag is reset, when
    bool _has_body = false;
    /// Contains true, if the body is transfered chunked
    bool _has_body_chunked = false;
    /// Contains true, if connection is upgraded
    bool _upgrade = false;
    /// Contains true, if the request has been modified, so it cannot be passed to different handler
    bool _touched = false;
    /// Contains true, if the headers has been already sent, so the response cannot be modified
    bool _headers_sent = false;
    /// contains size of the body, if the body is not chunked
    std::size_t _has_body_size = 0;

    ///contains buffer for sending headers
    std::vector<char> _send_header;
    ///contains temporary buffer for body
    std::string _tmp_body;

    ///contains true, if the body is chunked
    bool _output_chunked = false;
    /// contains size of body, if not chunked and size is known. If neither chunked nor size is known, the body is closed with EOF
    std::optional<std::size_t> _output_size;
    /// contains true, if Date header was set by set_header()
    bool _has_date = false;
    /// contains true, if Server header was set by set_header()
    bool _has_server = false;
    /// contsains true, if content type was set by set_header()
    bool _has_content_type = false;
    /// Contains true, if the connection was set by set_header()
    bool _has_connection = false;

    ///Reserved space for read callback
    coro::awaiting_callback<coro::awaitable<ReceiveBlockStatus>,
                ServerRequest *, coro::awaitable<bool>::result> _read_until_callback;
    ///Reserved space for send callback
    coro::awaiting_callback<coro::awaitable<bool>,
                ServerRequest *, std::string_view, coro::awaitable<bool>::result> _send_callback;


    void reset();
    bool parse2();


    awaitable<bool> send100();

    Stream prepare_body();
    Stream finish_send();

    void insert_send_header(std::string_view key, std::string_view value);
    std::string_view complete_headers();

    template<typename ... Args>
    auto send_helper(Args  ... args);


};




}

}
