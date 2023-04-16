#include "http_server_request.h"

#include "limited_stream.h"

#include "chunked_stream.h"
namespace coroserver {

namespace http {

ServerRequest::ServerRequest(Stream s)
    :_cur_stream(std::move(s))
    ,_load_awt(*this)
    ,_body_stream(nullptr)
    {}

cocls::future<bool> ServerRequest::load() {
    return [&](cocls::promise<bool> p) {
        _load_promise= std::move(p);
        _load_awt << [&]{
            return _cur_stream.read_until(_coro_storage, _header_data, "\r\n\r\n", 65536);
        };
    };
}

cocls::suspend_point<void> ServerRequest::parse_request(cocls::future<bool> &f) noexcept {
    try {
        _status_code = 0;
        //failed to load headers
        if (!*f) {
            //return false
            return _load_promise(false);
        }

        //parse standard headers and first line
        std::string_view first_line;
        if (!HeaderMap::headers(_header_data, _req_headers, first_line)) {
            //failed to load headers, return false
            _status_code = 400; //BAD REQUEST
            return _load_promise(false);
        }

        auto splt = splitAt(first_line, " ");
        std::string_view str_method = splt();
        std::string_view str_path = splt();
        std::string_view str_vers = splt();
        if (splt || str_method.empty() || str_vers.empty() || str_path.empty()) {
            _status_code = 400;
            return _load_promise(false);
        }

        _method = strMethod(str_method);
        if (_method == Method::unknown || _version == Version::unknown) {
            _status_code = 400;
            return _load_promise(false);
        }

        return _load_promise(parse_headers());

    } catch (...) {
        return _load_promise(std::current_exception());
    }
}

bool ServerRequest::parse_headers() {
    using namespace strtable;

    auto hv = _req_headers[hdr_host];
    if (hv.has_value()) _host = hv;
    _keep_alive =  (_version == Version::http1_1);

    hv = _req_headers[hdr_connection];
    if (hv.has_value()) {
      if (hv == val_close) _keep_alive = false;
      else if (hv == val_keep_alive) _keep_alive = true;
    }
    _expect_100_continue = false;
    hv = _req_headers[hdr_expect];
    if (hv.has_value()) {
      if (hv == val_100_continue) {
          _expect_100_continue = true;
      } else {
          _status_code = 417;
          return false;
      }
    }
    _has_body = false;
    if (_method == Method::GET || _method == Method::HEAD) {
        hv = _req_headers[hdr_content_length];
        if (hv.has_value()) {
            _status_code = 400;
            return false;
        }
        hv = _req_headers[hdr_transfer_encoding];
        if (hv.has_value()) {
            _status_code = 400;
            return false;
        }
        _body_stream = Stream(nullptr);
        return true;
    }
    hv = _req_headers[hdr_content_length];
    if (hv.has_value()) {
        std::size_t len = hv.get_uint();
        hv = _req_headers[hdr_transfer_encoding];
        if (hv.has_value()) {
            _status_code = 400;
            return false;
        }
        _body_stream = LimitedStream::read(_cur_stream, len);
        return true;
    }
    hv = _req_headers[hdr_transfer_encoding];
    if (hv.has_value()) {
        _has_body = true;
        if (hv == val_chunked) {
            _body_stream = ChunkedStream::read(_cur_stream);
            return true;
        } else {
            _status_code = 501;
            return false;
        }
    }
    _body_stream = Stream(nullptr);
    return true;
}

}

}
