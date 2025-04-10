#pragma once
#include "stream.hpp"
#include "http_common.h"
#include <vector>

namespace coroserver {

namespace http {

class ServerRequest {
public:

    ///Create server request
    /**
     * @param s connected stream, right connected, or TLS session established
     * @param server_name name of server. This is optional and specified
     * content of Server header. The string must be allocated while
     * this request exists, the best way to allocate name statically
     */
    ServerRequest(Stream s, std::string_view server_name = {});

    static constexpr std::size_t max_header_size = 65536;
    static constexpr std::size_t status_line_reservation = 100;
    static constexpr std::string_view header_separator = "\r\n\r\n";
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


    std::optional<std::size_t> get_header_uint(HeaderKey key) const;
    std::optional<std::size_t> get_content_length() const;
    std::optional<std::string_view> get_header(HeaderKey key) const;
    Stream get_source_stream() const;

    bool is_keep_alive() const {return _keep_alive && !_has_body && !_upgrade;}
    Method get_method() const {return _method;}
    const std::string_view& get_path() const {return _path;}
    Protocol get_protocol() const {return _protocol;}
    const std::vector<std::pair<HeaderKey, HeaderValue> >& get_recv_header() const {return _recv_header;}
    unsigned int get_status() const {return _status;}
    bool is_expect_100() const {return _expect_100;}
    const std::string_view& get_status_message() const {return _status_message;}
    bool is_upgrade() const {return _upgrade;}


    void set_header(HeaderKey key, std::string_view value);
    void set_header(HeaderKey key, std::size_t sz);
    void set_header_date_rfc5322(HeaderKey key, std::time_t tm);
    void set_status(unsigned int code);
    void set_status(unsigned int code, std::string_view message);
    void set_content_type(ContentType ctx);

    ///send response and body
    awaitable<bool> send(std::string_view response_body);
    ///send response make stream for sending body
    awaitable<Stream> send();

    const Stream& get_cur_stream() const {return _cur_stream;}

    void set_protocol(Protocol protocol) {_protocol = protocol;}

protected:

    Stream _cur_stream;
    std::string_view _server_name;
    std::vector<char> _recv_header_data;
    std::vector<std::pair<HeaderKey, HeaderValue> > _recv_header;
    Method _method = {};
    Protocol _protocol = {};
    std::string_view _path = {};
    unsigned int _status = 0;
    std::string_view _status_message = {};
    bool _keep_alive = true; //start with true to unblock load
    bool _expect_100 = false;
    bool _has_body = false;
    bool _has_body_chunked = false;
    bool _upgrade = false;
    std::size_t _has_body_size = 0;

    std::vector<char> _send_header;

    bool _output_chunked = false;
    std::optional<std::size_t> _output_size;
    bool _has_date = false;
    bool _has_server = false;
    bool _has_content_type = false;
    bool _has_connection = false;



    enum class SendState {
        status,
        headers,
        body,
    };

    SendState _send_state = SendState::status;

    coro::awaiting_callback<coro::awaitable<ReceiveBlockStatus>,
                ServerRequest *, Stream, coro::awaitable<bool>::result> _read_until_callback;
    coro::awaiting_callback<coro::awaitable<bool>,
                ServerRequest *, coro::awaitable<bool>::result> _send_callback;


    void reset();
    bool parse2();

    static bool compare_header(const std::pair<HeaderKey, HeaderValue> &a,
                               const std::pair<HeaderKey, HeaderValue> &b);

    awaitable<bool> send100();

    Stream prepare_body();

    void insert_send_header(std::string_view key, std::string_view value);
    void complete_headers();
    void set_date();
};

}

}
