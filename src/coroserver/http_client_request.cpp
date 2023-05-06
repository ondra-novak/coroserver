#include "http_client_request.h"
#include "http_stringtables.h"

#include "strutils.h"
#include "chunked_stream.h"
#include "limited_stream.h"

#include "exceptions.h"
namespace coroserver {

namespace http {


ClientRequest::ClientRequest(ClientRequestParams &params)
    :_s(std::move(params.s))
    ,_user_agent(params.user_agent)
    ,_host(params.host)
    ,_auth(params.auth)
    ,_static_headers(std::move(params.headers))
    ,_method(params.method)
    ,_request_version(params.ver)
    ,_after_send_headers_awt(*this)
    ,_receive_response_awt(*this) {
        prepare_header(params.method, params.path);
}

ClientRequest::ClientRequest(ClientRequestParams &&params):ClientRequest(params) {}

void ClientRequest::prepare_header(Method method, std::string_view path) {
     _req_headers << strMethod[method] << " " << path << " " << strVer[_request_version] << "\r\n";
     _owr_hdrs.clear();
     if (_static_headers) {
         for (std::size_t i = 0; i < _static_headers->size(); ++i) {
             _owr_hdrs.push_back(static_cast<std::uint8_t>(i));
         }
     }
     if (!_auth.empty()) owr_hdr(strtable::hdr_authorization);
     if (!_user_agent.empty()) owr_hdr(strtable::hdr_user_agent);

}


void ClientRequest::open(Method method, std::string_view path) {

    _status_code = 0;
    _status_message = {};
    _response_headers.clear();
    _response_headers_data.clear();
    _req_headers.str(std::string());
    _content_length = 0;
    _has_te = false;
    _is_te_chunked = false;
    _expect_100 = false;
    _req_sent = false;
    _resp_recv = false;
    _keep_alive = true;
    _custom_te= false;
    _body_stream = Stream(nullptr);
    _response_stream = Stream(nullptr);
    _body_to_write = {};
    _command = Command::none;
    _stream_promise(cocls::drop);
    _rcvstatus = 0;

    prepare_header(method, path);
}

constexpr auto reservedHeaders = makeStaticLookupTable<StringICmpView, int>({
    {strtable::hdr_content_length, 1},
    {strtable::hdr_transfer_encoding, 2},
    {strtable::hdr_expect, 3},
    {strtable::hdr_host, 4},
    {strtable::hdr_authorization, 5},
    {strtable::hdr_user_agent, 6}
});

void ClientRequest::owr_hdr(std::string_view hdr) {
    strIEqual eq;
    auto iter = std::find_if(_owr_hdrs.begin(),_owr_hdrs.end(),[&](const auto &x){
        return eq((*_static_headers)[x].first,hdr);
    });
    auto last = _owr_hdrs.end();
    if (iter != last) {
        --last;
        if (iter != last) std::swap(*iter, *last);
        _owr_hdrs.pop_back();
    }

}

ClientRequest&& ClientRequest::operator ()(std::string_view key, std::string_view value) {
    owr_hdr(key);
    int h = reservedHeaders.get(key, 0);
    switch (h) {
        default:break;
        case 1: _content_length = string2unsigned<std::size_t>(value.begin(), value.end(),10);
                return std::move(*this);
        case 2: if (_has_te) return std::move(*this);
                _has_te = true;
                if (strIEqual()(value, strtable::val_chunked)) {
                    _is_te_chunked = true;
                } else {
                    _custom_te = true;
                }
                break;
        case 3: if (strIEqual()(value, strtable::val_100_continue)) {
                    _expect_100 = true;
                    return std::move(*this);
                }
                break;
        case 4: _host = value;
                return std::move(*this);
        case 5: _auth = value;
                return std::move(*this);
        case 6: _user_agent= value;
                return std::move(*this);
    }
    _req_headers << key << ": " << value << "\r\n";
    return std::move(*this);
}

ClientRequest&& ClientRequest::operator ()(std::string_view key, std::size_t value) {
    owr_hdr(key);
    int h = reservedHeaders.get(key, 0);
    switch(h) {
        default: break;
        case 1: _content_length = value;
                return std::move(*this);
    }
    _req_headers << key << ": " << value << "\r\n";
    return std::move(*this);
}

ClientRequest &&ClientRequest::operator()(std::string_view key, std::nullptr_t) {
    owr_hdr(key);
    return std::move(*this);
}

ClientRequest &&ClientRequest::use_chunked() {
    if (!_has_te) {
        _req_headers << strtable::hdr_transfer_encoding << ": " << strtable::val_chunked << "\r\n";
        _has_te = true;
        _is_te_chunked = true;
    }
    return std::move(*this);
}

ClientRequest &&ClientRequest::content_length(std::size_t sz) {
    _content_length = sz;
    return std::move(*this);
}

ClientRequest &&ClientRequest::expect100continue() {
    _expect_100 = true;
    return std::move(*this);
}



cocls::future<bool> ClientRequest::send_headers() {
    _req_headers << strtable::hdr_host << ": " << _host << "\r\n";
    if (!_auth.empty()) {
        _req_headers << strtable::hdr_authorization << ": " << _host << "\r\n";
    }
    if (!_user_agent.empty()) {
        _req_headers << strtable::hdr_user_agent<< ": " << _user_agent<< "\r\n";
    }
    for (const auto &x: _owr_hdrs) {
        const auto &h = (*_static_headers)[x];
        _req_headers << h.first << ": " << h.second << "\r\n";
    }
    if (_method != Method::GET && _method != Method::HEAD) {
        if (!_has_te) {
            if (_content_length == 0) {
                _expect_100 = false;
            }
            _req_headers << strtable::hdr_content_length << ": " << _content_length << "\r\n";
        }
    }
    if (_expect_100) {
        _req_headers << strtable::hdr_expect << ": " << strtable::val_100_continue << "\r\n";
    }

    _req_headers << "\r\n";
    _req_sent = true;
    return _s.write(_req_headers.view());
}

cocls::future<Stream> ClientRequest::begin_body() {
    if (!_has_te && _content_length == 0) use_chunked();

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
            if (_status_code == 100) {
                cocls::future<bool> fakeres = cocls::future<bool>::set_value(true);
                return after_send_headers(fakeres);
            }
            prepare_response_stream();
            return _stream_promise(_response_stream);
    }
}

void ClientRequest::prepare_body_stream() {
    if (_is_te_chunked) _body_stream = ChunkedStream::write(_s);
    else if (_content_length) _body_stream = LimitedStream::write(_s, _content_length);
    else if (_custom_te) _body_stream = _s;
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
            if (_custom_te) {
                _receive_response_awt << [&]{return _s.read();};
            } else {
                _after_send_headers_awt << [&]{return _body_stream.write_eof();};
            }
        } else {
            _after_send_headers_awt << [&]{return send_headers();};
        }
    };
}



cocls::future<Stream> ClientRequest::send(std::string_view body) {
    if (!_req_sent) {
        if (_has_te) throw std::logic_error("Invalid request state: Transfer Encoding cannot be used when send(<body>) is called");
        return [&](auto promise) {
            content_length(body.size());
            _body_to_write = body;
            _command = Command::sendRequest;
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
            _content_length = 0;
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
