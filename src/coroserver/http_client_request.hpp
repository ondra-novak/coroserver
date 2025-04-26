#pragma once
#include "stream.hpp"
#include "http_common.hpp"
#include "http_status_exception.hpp"

#include <basic_coro/concepts.hpp>

#include <string_view>
#include <vector>
#include <span>

namespace coroserver {

namespace http {

class ClientRequest {
public:

    ///prepare HTTP client request
    /**
     * @param s connected stream
     * @param method http method
     * @param path URI/pathh
     * @param headers header of request
     * @param proto protocol
     */
    ClientRequest(Stream s);


    ///Open new HTTP request
    /**
     * This must be called prior to any other methods can be called. If the
     * object already contains request, it is reset.
     *
     * @param method reques method
     * @param path  uri path and query
     * @param proto protocol
     */
    void open(Method method,std::string_view path,Protocol proto = Protocol::HTTP_1_1);

    ///Add headers to prepared request
    /**
     * Note some headers are checked for duplication. Duplicated headers are discarded
     * @param key key
     * @param value value
     */
    void add_header(HeaderKey key, std::string_view value);
    ///Set content type
    /**
     * You can set content type only once. If content type is already set, cannot be changed
     * @param ctx
     */
    void set_content_type(ContentType ctx);

    ///Send request (empty body)
    /**
     * @return response body as stream. In case of network error, returns no-value
     * @note if the connection is upgraded, returned stream is the stream passed to the constructor
     *
     */
    awaitable<Stream> send();
    ///Send request (with body)
    /**
     * @return response body as stream. In case of network error, returns no-value
     */
    awaitable<Stream> send(std::string_view body);

    ///Send request streaming body
    /**
     * @param stream_source the function is called repeatedly to
     * for data. To stop cycle, return empty buffer. The function can
     * be awaitable (it is required to return std::string_view or awaiter
     * returning std::string_view)
     *
     *
     * @return response body as stream. In case of network error, returns no-value
     *
     * @note This function is supported only for HTTP/1.1 and it uses
     * chunked stream
     */
    template<std::invocable<> StreamSource>
    awaitable<Stream> send_stream(StreamSource &&stream_source) {
        if constexpr(coro::is_awaitable<std::invoke_result_t<StreamSource> >) {
            static_assert(std::is_convertible_v<coro::awaitable_result<std::invoke_result_t<StreamSource> >, std::string_view>,
            "Result of StreamSource must be convertible to std::string_view");
        } else {
            static_assert(std::is_convertible_v<std::invoke_result_t<StreamSource>,std::string_view>,
            "Result of StreamSource must be convertible to std::string_view");
        }
        return send_stream2(_alloc, std::forward<StreamSource>(stream_source));
    }


    HeaderValue get_header(HeaderKey key) const;
    std::optional<std::size_t> get_header_uint(HeaderKey key) const;

    unsigned int get_status() const;
    std::string_view get_status_message() const;

    ///Connection has keep alive active
    /**
     * You must process whole body to reuse this request object
     * @retval true keep alive is enabled
     * @retval false keep alive is disabled
     */
    bool is_keep_alive() const;
    ///Connection has been upgrated
    /**
     * @retval true connection has been upgrated (websocket etc)
     * @retval false normal HTTP request
     */
    bool is_upgrated() const;


    ///Retrieve content type if it is known
    ContentType get_content_type() const;

    ///export response as StatusException
    /**
     * @note lefts object unusable.
     * @return exception object
     */
    StatusException as_exception();

    ///Throw status as exception
    /**
     * @note lefts object unusable.
     */
    [[noreturn]] void throw_as_exception() {
        throw as_exception();
    }

protected:

    static constexpr std::size_t max_header_size = 65536;


    Stream _stream;
    unsigned int _status;
    Protocol _proto;
    std::string_view _status_message;
    std::vector<char> _header_data;
    std::vector<std::pair<HeaderKey, std::string_view> > _headers;

    bool _has_content_type = false;
    bool _has_host = false;
    bool _has_user_agent = false;
    bool _has_expect_100 = false;
    Method _method = Method::unknown;

    bool _upgraded = false;
    bool _keep_alive = false;


    void add_header_internal(const HeaderKey &key,
            const std::string_view &value);

    coro::reusable_allocator _alloc;

    awaitable<Stream> send2(coro::reusable_allocator &, std::string_view body);
    template<std::invocable<> Fn>
    awaitable<Stream> send_stream2(coro::reusable_allocator &, Fn &&source);

    std::string_view finish_header();

    Stream prepare_body();
    bool parse_input_headers();

    Stream get_chunked_stream();
};


template<std::invocable<> StreamSource>
inline awaitable<Stream> coroserver::http::ClientRequest::send_stream2(
                        coro::reusable_allocator &, StreamSource &&source) {

    add_header_internal("Transfer-Encoding", "chunked");
    auto hdr = finish_header();
    bool b = co_await _stream.write(hdr);
    if (!b) co_return std::nullopt;
    _header_data.clear();
    if (_has_expect_100) {
        b = co_await _stream.read_until(_header_data, header_block_separator, max_header_size);
        if (!b) co_return std::nullopt;
        if (!parse_input_headers()) co_return std::nullopt;
        if (_status != 100) {
            co_return [&]{return prepare_body();};
        }
    }

    Stream chks = get_chunked_stream();

    while (true) {
        std::string_view data;
        if constexpr(coro::is_awaitable<std::invoke_result_t<StreamSource> >) {
            data = co_await source();
        } else {
            data = source();
        }
        if (data.empty()) break;
        co_await chks.write(data);
    }

    b = co_await _stream.read_until(_header_data, header_block_separator, max_header_size);
    if (!b) co_return std::nullopt;

    if (!parse_input_headers()) co_return std::nullopt;
    co_return [&]{return prepare_body();};




}

}


}
