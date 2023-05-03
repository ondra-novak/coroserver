#include "http_client_request.h"

#include "strutils.h"
#include "chunked_stream.h"
#include "limited_stream.h"

#include "exceptions.h"
namespace coroserver {

namespace http {

ClientRequest::ClientRequest(Stream s, std::string_view user_agent)
        :_s(std::move(s))
        ,_user_agent(user_agent)
        ,_after_send_headers_awt(*this)
        ,_receive_response_awt(*this)
        {}

ClientRequest::ClientRequest(Stream s, Method method, std::string_view host,
        std::string_view path, std::string_view user_agent, Version ver):ClientRequest(std::move(s), user_agent) {
    gen_first_line(method, host, path, ver);
}

ClientRequest::ClientRequest(const ClientRequestParams &params)
    :ClientRequest(params.s, params.user_agent) {
    gen_first_line(params.method, params.host, params.path, params.ver);
    _auth = params.auth;
}
void ClientRequest::open(Method method, std::string_view host, std::string_view path, Version ver) {
    _status_code = 0;
    _req_headers.str(std::string());
    _content_length = 0;
    _has_content_len = false;
    _has_te = false;
    _is_te_chunked = false;
    _expect_100 = false;
    _req_sent = false;
    _resp_recv = false;
    _body_stream = Stream(nullptr);
    _response_stream = Stream(nullptr);
    _response_headers_data.clear();
    _response_headers.clear();
    gen_first_line(method, host, path, ver);
}

ClientRequest&& ClientRequest::operator ()(std::string_view key, std::string_view value) {
    strIEqual eq;
    if (eq(key, strtable::hdr_content_length)) {
        if (_has_content_len || _has_te) return std::move(*this);
        _content_length = string2unsigned<std::size_t>(value.begin(), value.end(),10);
        _has_content_len = true;
    } else if (eq(key, strtable::hdr_transfer_encoding)) {
        if (_has_content_len || _has_te) return std::move(*this);
        _has_te = true;
        if (eq(value, strtable::val_chunked)) {
            _is_te_chunked = true;
        }
    } else if (eq(key, strtable::hdr_expect) && eq(value, strtable::val_100_continue)) {
        if (_expect_100) return std::move(*this);
        _expect_100 = true;
    }
    _req_headers << key << ": " << value << "\r\n";
    return std::move(*this);
}

ClientRequest&& ClientRequest::operator ()(std::string_view key, std::size_t value) {
    strIEqual eq;
    if (eq(key, strtable::hdr_content_length)) {
        if (_has_content_len || _has_te) return std::move(*this);
        _content_length = value;
        _has_content_len  = true;
    }
    _req_headers << key << ": " << value << "\r\n";
    return std::move(*this);
}

ClientRequest &&ClientRequest::use_chunked() {
    if (!_is_te_chunked && !_has_content_len) {
        _req_headers << strtable::hdr_transfer_encoding << ": " << strtable::val_chunked << "\r\n";
        _is_te_chunked = true;
    }
    return std::move(*this);
}

ClientRequest &&ClientRequest::content_length(std::size_t sz) {
    if (!_is_te_chunked && !_has_content_len) {
        _content_length = sz;
    }
    return std::move(*this);
}

ClientRequest &&ClientRequest::expect100continue() {
    if (!_expect_100) {
        _req_headers << strtable::hdr_expect << ": " << strtable::val_100_continue << "\r\n";
        _expect_100 = true;
    }
    return std::move(*this);
}


void ClientRequest::gen_first_line(Method method, std::string_view host,
        std::string_view path, Version ver) {

    _method = method;
    _req_headers << strMethod[method] << " " << path << " " << strVer[ver] << "\r\n";
    _req_headers << strtable::hdr_host << ": "  << host << "\r\n";
}

cocls::future<bool> ClientRequest::send_headers(std::string_view body) {
    if (_method != Method::GET && _method != Method::HEAD) {
        if (!_has_content_len && !_is_te_chunked) {
            _req_headers << strtable::hdr_content_length << ": " << _content_length << "\r\n";
        }
    }
    if (!_user_agent.empty()) {
        _req_headers << strtable::hdr_user_agent << ": " << _user_agent << "\r\n";
    }
    if (!_auth.empty()) {
        _req_headers << strtable::hdr_authorization << ": " << _auth << "\r\n";
    }
    _req_headers << "\r\n";
    _req_sent = true;
    _body_to_write = body;
    return _s.write(_req_headers.view());
}

cocls::future<Stream> ClientRequest::begin_body() {
    if (!_has_te && !_has_content_len) use_chunked();

    return [&](auto promise) {
        if (_req_sent) promise(_body_stream);
        else {
            _command = Command::beginBody;
            _stream_promise = std::move(promise);
            _after_send_headers_awt << [&]{return send_headers();};
        };
    };

}

static constexpr search_kmp<4> end_of_header("\r\n\r\n");

cocls::suspend_point<void> ClientRequest::receive_response(cocls::future<std::string_view> &res) noexcept {
    try {
        std::string_view data = *res;
        if (data.empty()) throw ConnectionReset();
        for (std::size_t i = 0, cnt = data.size(); i < cnt; i++) {
            _response_headers_data.push_back(data[i]);
            if (end_of_header(_rcvstatus, data[i])) {
                _response_headers_data.pop_back();
                _response_headers_data.pop_back();
                _s.put_back(data.substr(i+1));
                return after_receive_headers();
            }
        }
        _receive_response_awt << [&]{return _s.read();};
        return {};
    } catch (...) {
        return _stream_promise(std::current_exception());
    }
}

cocls::suspend_point<void> ClientRequest::after_receive_headers() {
    std::string_view first_line;
    if (!HeaderMap::headers({_response_headers_data.data(),_response_headers_data.size()}, _response_headers, first_line)) {
        throw InvalidFormat();
    }
    auto splt = splitAt(first_line, " ");
    _response_version = strVer[splt()];
    auto status_code_str = splt();
    _status_code = string2signed<int>(status_code_str.begin(), status_code_str.end(), 10);
    _status_message = splt;

    switch (_command) {
        default: throw std::logic_error("Invalid state");
        case Command::beginBody:
            if (_status_code == 100) {
                prepare_body_stream();
            } else {
                _body_stream = Stream::null_stream();
                prepare_response_stream();
            }
            return _stream_promise(_body_stream);
        case Command::sendRequest:
            prepare_response_stream();
            return _stream_promise(_response_stream);
    }
}

void ClientRequest::prepare_body_stream() {
    if (_is_te_chunked) _body_stream = ChunkedStream::write(_s);
    else if (_content_length) _body_stream = LimitedStream::write(_s, _content_length);
    else _body_stream = Stream::null_stream();
    _is_te_chunked = false;
    _content_length = 0;
}

cocls::future<Stream> ClientRequest::begin_body(std::size_t ctl) {
    content_length(ctl);
    return begin_body();
}

cocls::future<Stream> ClientRequest::send() {
    return [&](auto promise) {
        _stream_promise = std::move(promise);
        _command = Command::sendRequest;

        if (_resp_recv) {
            _stream_promise(_response_stream);
        } else if (_req_sent) {
            _after_send_headers_awt << [&]{return _body_stream.write_eof();};
        } else {
            _after_send_headers_awt << [&]{return send_headers();};
        }
    };
}



cocls::future<Stream> ClientRequest::send(std::string_view body) {
    if (!_req_sent) {
        return [&](auto promise) {
            content_length(body.size());
            _command = Command::beginBody;
            _stream_promise = std::move(promise);
            _after_send_headers_awt << [&]{return send_headers();};
        };
    } else {
        return send();
    }
}


void ClientRequest::prepare_response_stream() {
    strIEqual eq;
    auto hdrval = _response_headers[strtable::hdr_content_length];
    if (hdrval.has_value()) {
        _response_stream = LimitedStream::read(_s, hdrval.get_uint());
        _keep_alive = true;
    } else if (eq(hdrval = _response_headers[strtable::hdr_transfer_encoding], strtable::val_chunked)) {
        _response_stream = ChunkedStream::read(_s);
        _keep_alive = true;
    } else {
        _response_stream = _s;
        _keep_alive = false;
    }

    if (_response_version != Version::http1_1) {
        hdrval = _response_headers[strtable::hdr_connection];
        if (!eq(hdrval, strtable::val_keep_alive)) _keep_alive = false;
    } else {
        hdrval = _response_headers[strtable::hdr_connection];
        if (eq(hdrval, strtable::val_close)) _keep_alive = false;
    }
    _resp_recv = true;
}

cocls::suspend_point<void> ClientRequest::after_send_headers(cocls::future<bool> &res) noexcept {
    try {
        bool r = *res;
        if (!r) throw ConnectionReset();

        if (!_body_to_write.empty()) {
            auto s = _body_to_write;
            _body_to_write = {};
            _after_send_headers_awt << [&]{return _s.write(s);};
            return {};
        }

        _rcvstatus = 0;
        _response_headers_data.clear();

        switch (_command) {
            default: throw std::logic_error("Invalid state");
            case Command::beginBody:
                if (_expect_100) {
                    _receive_response_awt << [&]{return _s.read();};
                    return {};
                } else {
                    prepare_body_stream();
                    return _stream_promise(_body_stream);
                }
            case Command::sendRequest:
                if (_content_length>0 || _is_te_chunked) {
                    prepare_body_stream();
                    _after_send_headers_awt << [&]{return _body_stream.write_eof();};
                } else {
                    _receive_response_awt << [&]{return _s.read();};
                }
                return {};
        }
    } catch (...) {
       _req_sent = false;
       return _stream_promise(std::current_exception());
    }
}

}


}
