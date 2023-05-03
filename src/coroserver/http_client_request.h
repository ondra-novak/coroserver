#pragma once
#ifndef SRC_COROSERVER_HTTP_CLIENT_REQUEST_H_
#define SRC_COROSERVER_HTTP_CLIENT_REQUEST_H_
#include "http_common.h"
#include "stream.h"

#include <cocls/generator.h>

namespace coroserver {

namespace http {


struct ClientRequestParams {
    Stream s;
    Method method;
    std::string_view host;
    std::string_view path;
    std::string_view user_agent;
    std::string auth;
    Version ver;
};

class ClientRequest {
public:

    ///Create empty client request
    ClientRequest(Stream s, std::string_view user_agent = {});

    ///Create client request and open it
    /**
     * @param method method
     * @param host host
     * @param path path (must start by /)
     * @param ver version (optional)
     *
     * @note opening by constructor is slightly faster, than method open()
     */
    ClientRequest(Stream s, Method method, std::string_view host, std::string_view path, std::string_view user_agent = {}, Version ver = Version::http1_1);


    ///Construct client request indirectly;
    ClientRequest(const ClientRequestParams &params);

    ///Open new request
    /**
     * @param method method
     * @param host host
     * @param path path (must start by /)
     * @param ver version (optional)
     */
    void open(Method method, std::string_view host, std::string_view path, Version ver = Version::http1_1);

    ///Set header value
    /**
     * @param key header key
     * @param value header value
     * @return reference to this to chain next header
     *
     * @note Headers aren't deduplicated
     */
    ClientRequest &&operator()(std::string_view key, std::string_view value);
    ///Set header value
    /**
     * @param key header key
     * @param value header value
     * @return reference to this to chain next header
     *
     * @note Headers aren't deduplicated
     *
     */
    ClientRequest &&operator()(std::string_view key, std::size_t value);


    ///Send body using chunked protocol
    /**
     * Chunked protocol allows to send infinite stream.
     *
     * @return this
     */
    ClientRequest &&use_chunked();


    ///Specify expected length of the body
    ClientRequest &&content_length(std::size_t sz);

    ///Request 100 continue to send body
    /**
     * Note in this case, you need to check status after you call begin_body(). If
     * the status is 100, continue to send body. If the status is different, you
     * already has response. In this case, just call send() to retrieve the response stream
     *
     * @return
     */
    ClientRequest &&expect100continue();

    ///Sends headers and begin sending of the body
    /**
     * For method which sends the body. You need to set properly headers Content-Length
     * or Transfer Encoding
     *
     * @return stream to be used to send the body.
     *
     * @note to finish body, call send() and receive response
     *
     * @note without content length header the function generates chunked body. For
     * compatibility reason it is recommended to set content length, or use
     * function begin_body with the argument
     */

    cocls::future<Stream> begin_body();

    ///Sends headers and begin sending of the body
    /**
     * For method which sends the body. You need to set properly headers Content-Length
     * or Transfer Encoding
     *
     * @param content_length content length.
     * @return stream to be used to send the body.
     *
     * @note to finish body, call send() and receive response
     *
     * @note if expect100continue is active, you need to check status after return.
     * To continue sending body, there should be status 100
     *
     */
    cocls::future<Stream> begin_body(std::size_t content_length);


    ///Send request or finish the body and read response.
    /**
     * @return response stream. Function also sets response headers and status code
     */
    cocls::future<Stream> send();
    ///Send request with body
    /**
     * @param body body to send.
     * @return response stream. Function also sets response headers and status code
     */
    cocls::future<Stream> send(std::string_view body);


    ///Retrieve response status code
    int get_status() const {return _status_code;}

    ///Retrieve status message
    std::string_view get_status_message() const {return _status_message;}

    ///Retrieve response version
    Version get_version() const {return _response_version;}
    ///retrieve a header
    HeaderValue operator[](const std::string_view &key) const {
        return _response_headers[key];
    }
    ///retrieve all headers
    const HeaderMap &headers() const {
        return _response_headers;
    }

    ///returns true, if connection is kept alive
    bool is_kept_alive() const;




protected:
    Stream _s;
    std::string _user_agent;
    std::string _auth;
    int _status_code = 0;
    std::string_view _status_message;
    Method _method = Method::not_set;
    Version _response_version = Version::http1_0;
    HeaderMap _response_headers;
    std::vector<char> _response_headers_data;
    std::ostringstream _req_headers;
    std::size_t _content_length = 0;
    bool _has_te = false;
    bool _is_te_chunked = false;
    bool _has_content_len = false;
    bool _expect_100 = false;
    bool _req_sent = false;
    bool _resp_recv = false;
    bool _keep_alive = true;
    Stream _body_stream = {nullptr};
    Stream _response_stream = {nullptr};


    enum class Command {
        none,
        beginBody,
        sendRequest,
    };


    void gen_first_line(Method method, std::string_view host, std::string_view path, Version ver);

    cocls::suspend_point<void> after_send_headers(cocls::future<bool> &res) noexcept;
    cocls::suspend_point<void> receive_response(cocls::future<std::string_view> &res) noexcept;
    std::size_t _rcvstatus = 0;
    cocls::call_fn_future_awaiter<&ClientRequest::after_send_headers> _after_send_headers_awt;
    cocls::call_fn_future_awaiter<&ClientRequest::receive_response> _receive_response_awt;
    cocls::promise<Stream> _stream_promise;
    Command _command = Command::none;
    std::string_view _body_to_write;



    cocls::future<bool> send_headers(std::string_view body = {});
    void prepare_body_stream();
    void prepare_response_stream();
    cocls::suspend_point<void> after_receive_headers();

};

}


}




#endif /* SRC_COROSERVER_HTTP_CLIENT_REQUEST_H_ */
