#include "null_stream.h"
#include "limited_stream.hpp"
#include "chunked_stream.hpp"
#include "http_client_request.hpp"

#include <format>

namespace coroserver {

namespace http {

ClientRequest::ClientRequest(Stream s)
        :_stream(s) {}

void ClientRequest::open(Method method,std::string_view path,Protocol proto) {
    _header_data.clear();
    _headers.clear();
    _has_content_type = false;
    _has_host = false;
    _has_user_agent = false;
    _has_expect_100 = false;
    _head_method = method == Method::HEAD;
    _status_message = {};
    _status = 0;
    std::format_to(std::back_inserter(_header_data), "{} {} {}\r\n",
            std::string_view(methods[method]), path, std::string_view(protocols[proto])
    );
}

void ClientRequest::add_header_internal(const HeaderKey &key,
        const std::string_view &value) {
    _header_data.insert(_header_data.end(), key.begin(), key.end());
    _header_data.insert(_header_data.end(), header_keyvalue_separator_with_space.begin(), header_keyvalue_separator_with_space.end());
    _header_data.insert(_header_data.end(), value.begin(), value.end());
    _header_data.insert(_header_data.end(), header_row_separator.begin(), header_row_separator.end());
}

void ClientRequest::add_header(HeaderKey key, std::string_view value) {
    if (_header_data.empty()) return;
    if (key == "Content-Type") {
        if (_has_content_type) return;
        _has_content_type = true;
    } else if (key == "Host") {
        if (_has_host) return ;
        _has_host = true;
    } else if (key == "User-Agent") {
        if (_has_user_agent) return;
        _has_user_agent = true;
    } else if (key == "Expect" && HeaderKey(value) == "100-continue") {
        _has_expect_100 = true;
    } else if (key == "Content-Length" || key == "Transfer-Encoding") {
        //these are forbidden headers
        return;
    }

    add_header_internal(key, value);
}

void ClientRequest::set_content_type(ContentType ctx) {
    add_header("Content-Type", content_types[ctx]);
}

awaitable<Stream> ClientRequest::send() {
    return send(std::string_view{});
}

awaitable<Stream> ClientRequest::send(std::string_view body) {
    return send2(_alloc, body);
}

std::string_view ClientRequest::finish_header() {
    _header_data.insert(_header_data.end(), header_row_separator.begin(), header_row_separator.end());
    return std::string_view(_header_data.data(), _header_data.size());

}

awaitable<Stream> ClientRequest::send2(coro::reusable_allocator &, std::string_view body) {
    add_header_internal("Content-Length", std::to_string(body.size()));
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
    b = co_await _stream.write(body);
    if (!b) co_return std::nullopt;

    b = co_await _stream.read_until(_header_data, header_block_separator, max_header_size);
    if (!b) co_return std::nullopt;

    if (!parse_input_headers()) co_return std::nullopt;
    co_return [&]{return prepare_body();};

}

HeaderValue ClientRequest::get_header(HeaderKey key) const {
    auto iter = std::lower_bound(_headers.begin(), _headers.end(), std::pair(key, std::string_view()), &compare_header);
    if (iter == _headers.end() || iter->first != key) return std::nullopt;
    return iter->second;
}

std::optional<std::size_t> ClientRequest::get_header_uint(HeaderKey key) const {
    auto r = get_header(key);
    if (!r) return std::nullopt;
    std::size_t sz;
    auto pp = std::from_chars(r->begin(), r->end(), sz, 10);
    if (pp.ec != std::errc() || pp.ptr != r->end()) return std::nullopt;
    return sz;
}

bool ClientRequest::is_keep_alive() const {
    return _keep_alive;
}

bool ClientRequest::is_upgrated() const {
    return _upgraded;
}

Stream ClientRequest::prepare_body() {
    if (_head_method) return NullStream::create();
    if (_upgraded) return _stream;
    auto te = get_header("Transfer-Encoding");
    auto cl = get_header_uint("Content-Length");
    if (cl) {
        if (te && HeaderKey(*te) == "chunked") {
            _keep_alive = false;
            return _stream;
        }
        if (*cl == 0) return NullStream::create();
        else return LimitedStream::create_read_limited(_stream, *cl);
    } else if (te && HeaderKey(*te) == "chunked") {
        return ChunkedStream::create(_stream);
    } else {
        return _stream;
    }
}

bool ClientRequest::parse_input_headers() {
    std::string_view data(_header_data.begin(), _header_data.end());
    auto fline = split_at(data, header_row_separator);
    while (!data.empty()) {
        auto rw = split_at(data, header_row_separator);
        auto k = trim(split_at(rw, ":"));
        auto v = trim(rw);
        _headers.emplace_back(k,v);
    }
    auto s_proto = split_at(fline, " ");
    auto s_status = split_at(fline, " ");
    auto message = trim(fline);

    _proto = protocols[s_proto];
    if (_proto == Protocol::unknown) return false;
    auto pp = std::from_chars(s_status.begin(), s_status.end(), _status, 10);
    if (pp.ec != std::errc() || pp.ptr != s_status.end()) return false;
    _status_message = message;


    std::sort(_headers.begin(), _headers.end(), &compare_header);

    _upgraded = false;
    auto val = get_header("Connection");
    if (val.has_value()) {
        HeaderKey iv(*val);
        if (iv == "close") _keep_alive = false;
        else if (iv == "keep-alive") _keep_alive = true;
        else if (iv == "upgrade") {
            _keep_alive = false;
            _upgraded= true;
        } else {
            return false;
        }
    } else {
        _keep_alive = _proto == Protocol::HTTP_1_1;
    }

    return true;

}

ContentType ClientRequest::get_content_type() const {
    auto c = get_header("Content-Type");
    if (c) {
        auto sp = trim(split_at(*c, ";"));
        return content_types[sp];
    }
    return ContentType::octet_stream;
}

StatusException ClientRequest::as_exception() {
    if (_upgraded) return StatusException(_status, std::string(_status_message));
    _keep_alive = false;
    return StatusException(_status, std::string(_status_message), get_content_type(), std::move(_stream));
}

Stream ClientRequest::get_chunked_stream() {
    return ChunkedStream::create(_stream);
}

}

}
