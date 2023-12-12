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
    ,_request_version(params.ver) {
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
    _stream_promise.drop();
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



coro::future<bool> ClientRequest::send_headers() {
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

coro::lazy_future<Stream> ClientRequest::begin_body() {
    if (!_has_te && _content_length == 0) use_chunked();
    auto &t = _lazy_target.on_activate<coro::lazy_future<Stream>::promise_target_type>([&](auto promise){
        if (_req_sent) promise(_body_stream);
        else {
            _command = Command::beginBody;
            _stream_promise = std::move(promise);
            auto &t = _target.on_activate<coro::future<bool>::target_type>([&](auto fut) {
                after_send_headers(fut);
            });
            _write_fut << [&]{return send_headers();};
            _write_fut.register_target(t);
        };
    });
    return t;

}

static constexpr search_kmp<4> end_of_header("\r\n\r\n");

void ClientRequest::receive_response(coro::future<std::string_view> *res) noexcept {
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
        _read_fut << [&]{return _s.read();};
        _read_fut.register_target(
                _target.on_activate<coro::future<std::string_view>::target_type>(
                        [&](auto fut) {receive_response(fut);}));

    } catch (...) {
        _stream_promise.reject();
    }
}

void ClientRequest::after_receive_headers() {
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
            _stream_promise(_body_stream);
            return;
        case Command::sendRequest:
            if (_status_code == 100) {
                coro::future<bool> fakeres(true);
                return after_send_headers(&fakeres);
            }
            prepare_response_stream();
            _stream_promise(_response_stream);
            return;
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

coro::lazy_future<Stream> ClientRequest::begin_body(std::size_t ctl) {
    content_length(ctl);
    return begin_body();
}

coro::lazy_future<Stream> ClientRequest::send() {
    return _lazy_target.on_activate<coro::lazy_future<Stream>::promise_target_type>(
          [&](auto promise) {
        _stream_promise = std::move(promise);
        _command = Command::sendRequest;

        if (_resp_recv) {
            _stream_promise(_response_stream);
        } else if (_req_sent) {
            if (_custom_te) {
                _read_fut << [&]{return _s.read();};
                _read_fut.register_target(
                        _target.on_activate<coro::future<std::string_view>::target_type>(
                                [&](auto fut){receive_response(fut);}
                ));
            } else {
                _write_fut << [&]{return _body_stream.write_eof();};
                _write_fut.register_target(
                        _target.on_activate<coro::future<bool>::target_type>(
                                [&](auto fut){after_send_headers(fut);}
                ));


            }
        } else {
            _write_fut << [&]{return send_headers();};
            _write_fut.register_target(
                        _target.on_activate<coro::future<bool>::target_type>(
                                [&](auto fut){after_send_headers(fut);}
            ));

        }
    });
}



coro::lazy_future<Stream> ClientRequest::send(std::string_view body) {
    if (!_req_sent) {
        if (_has_te) throw std::logic_error("Invalid request state: Transfer Encoding cannot be used when send(<body>) is called");
        return _lazy_target.on_activate<coro::lazy_future<Stream>::promise_target_type>(
                [&](auto promise) {
                    content_length(body.size());
                    _body_to_write = body;
                    _command = Command::sendRequest;
                    _stream_promise = std::move(promise);
                    _write_fut << [&]{return send_headers();};;
                    _write_fut.register_target(
                            _target.on_activate<coro::future<bool>::target_type>(
                                    [&](auto fut){after_send_headers(fut);}
                    ));
        });
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

void ClientRequest::after_send_headers(coro::future<bool> *res) noexcept {
    try {
        bool r = *res;
        if (!r) throw ConnectionReset();

        if (!_body_to_write.empty()) {
            auto s = _body_to_write;
            _body_to_write = {};
            _content_length = 0;
            _write_fut << [&]{return _s.write(s);};
            _write_fut.register_target(_target.on_activate<coro::future<bool>::target_type>(
                    [&](auto fut){return after_send_headers(fut);}
            ));
            return;
        }

        _rcvstatus = 0;
        _response_headers_data.clear();

        switch (_command) {
            default: throw std::logic_error("Invalid state");
            case Command::beginBody:
                if (_expect_100) {
                    _read_fut << [&]{return _s.read();};
                    _read_fut.register_target(_target.on_activate<coro::future<std::string_view>::target_type>(
                            [&](auto fut){receive_response(fut);}
                    ));
                    return;
                } else {
                    prepare_body_stream();
                    _stream_promise(_body_stream);
                    return;
                }
            case Command::sendRequest:
                if (_content_length>0 || _is_te_chunked) {
                    prepare_body_stream();
                    _write_fut << [&]{return _body_stream.write_eof();};
                    _write_fut.register_target(_target.on_activate<coro::future<bool>::target_type>(
                            [&](auto fut){after_send_headers(fut);}
                    ));

                } else {
                    _read_fut << [&]{return _s.read();};
                    _read_fut.register_target(_target.on_activate<coro::future<std::string_view>::target_type>(
                            [&](auto fut){receive_response(fut);}
                    ));
                }
                return;
        }
    } catch (...) {
       _req_sent = false;
       _stream_promise.reject();
    }
}

}


}
