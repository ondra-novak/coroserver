#include "http_server_request.hpp"

#include "null_stream.h"

#include "limited_stream.hpp"

#include "chunked_stream.hpp"
#include <charconv>
using coroserver::LimitedStream;
namespace coroserver {

namespace http {




awaitable<bool> ServerRequest::parse(Stream s) {
    reset();
    auto awt = s.read_until(_recv_header_data, header_separator, max_header_size);
    if (awt.is_ready()) {
        return  parse2(std::move(s));
    } else {
        _read_until_callback.set_awaiter(std::move(awt));
        return [this,s = std::move(s)](coro::awaitable<bool>::result r) mutable {
            if (!r) {
                _read_until_callback.get_awaiter().cancel();
                return r.set_empty();
            }
            return _read_until_callback.await([this, r = std::move(r), s = std::move(s)](auto &awt) mutable {
                try {
                    if (!awt.has_value()) return r.set_empty();
                    bool st = awt.await_resume();
                    if (!st) return r.set_empty();
                    return r(parse2(std::move(s)));
                } catch (...) {
                    return r.set_exception(std::current_exception());
                }
            });
        };
    }
}

Stream ServerRequest::get_source_stream() const {
    return _cur_stream;
}

bool ServerRequest::compare_header(const std::pair<HeaderKey, HeaderValue> &a,
                           const std::pair<HeaderKey, HeaderValue> &b) {
    return a.first < b.first;
}


std::optional<std::size_t> ServerRequest::get_header_uint(HeaderKey key) const {
    auto s = get_header(key);
    if (s.has_value()) {
        auto b = s->data();
        auto e = s->data()+s->size();
        std::size_t sz;
        auto r = std::from_chars( b, e, sz, 10);
        if (r.ec != std::errc{} || r.ptr != e) return std::nullopt;
        return sz;
    } else {
        return std::nullopt;
    }
}

std::optional<std::size_t> ServerRequest::get_content_length() const {
    return get_header_uint("Content-Length");
}

std::optional<std::string_view> ServerRequest::get_header(HeaderKey key) const {
    auto iter = std::lower_bound(_recv_header.begin(), _recv_header.end(), std::pair(key, std::string_view()), compare_header);
    if (iter == _recv_header.end() || iter->first != key) return std::nullopt;
    return iter->second;
}

bool ServerRequest::parse2(Stream s) {
    std::optional<Stream> retval;
    std::string_view data(_recv_header_data.data(), _recv_header_data.size());
    auto first_line = trim(split_at(data, "\r\n"));
    while (data.empty()) {
        auto ln = split_at(data, "\r\n");
        if (ln.empty()) continue;
        auto k = trim(split_at(ln,":"));
        auto v = trim(ln);
        _recv_header.emplace_back(k,v);
    }
    auto tmp = split_at(first_line, " ");
    _method = methods[tmp];
    _path = split_at(first_line, " ");
    _protocol = protocols[first_line];

    _has_body = false;


    if (_method == Method::unknown || _protocol == Protocol::unknown) {
        return false;
    }

    std::sort(_recv_header.begin(), _recv_header.end(), compare_header);

    _keep_alive = (_protocol == Protocol::HTTP_1_1);

    auto hconn = get_header("Connection");
    if (hconn.has_value()) {
        HeaderKey v(*hconn);
        if (v == "close") _keep_alive = false;
        else if (v == "keep-alive") _keep_alive = true;
        else if (v == "upgrade") {
            _upgrade = true;
        }
        else return false;
    }

    _expect_100 = false;
    auto hexcept = get_header("Except");
    if (hexcept.has_value()) {
        HeaderKey v(*hexcept);
        if (v == "100-continue") {
            _expect_100 = true;
        }
    }

    bool can_have_body = _method != Method::GET && _method != Method::HEAD;
    auto hcl = get_content_length();
    auto hte = get_header("Transfer-Encoding");
    if (*hcl) {
        if (!hte.has_value() && can_have_body && !_upgrade) {
            if (*hcl) { //only nonzero body
                _has_body = true;
                _has_body_size = *hcl;
            } else {    //this request has no body
                _has_body = false;
            }
        } else {
            return false;
        }
    } else if (hte.has_value()) {
        if (!hcl.has_value() && can_have_body && !_upgrade) {
            HeaderKey v(*hte);
            if (v == "chunked") {
                _has_body = true;
                _has_body_chunked = true;
            } else {
                return false;
            }
        } else {
            return false;
        }
    } else if (can_have_body && !_upgrade){
        return false;
    }
    _cur_stream = std::move(s);
    return true;
}

awaitable<Stream> ServerRequest::get_body() {
    if (_expect_100) {
        _expect_100 = false;
        auto awt = send100();
        if (awt.await_ready()) {
            return prepare_body();
        }
        _send_callback.set_awaiter(awt);
        return [this](awaitable<Stream>::result r){
            if (!r) {
                _send_callback.get_awaiter().cancel();
                return r();
            }
            return _send_callback.await([this, r = std::move(r)](auto &awt) mutable{
                try {
                    if (awt.await_resume()) {
                        return r(prepare_body());
                    } else {
                        return r.set_empty();
                    }
                } catch (...) {
                    return r.set_exception(std::current_exception());
                }
            });
        };
    } else {
        return prepare_body();
    }
}

awaitable<bool> ServerRequest::send100() {
    if (_protocol == Protocol::HTTP_1_0) {
        return _cur_stream.write("HTTP/1.0 100 Continue\r\n\r\n");
    } else {
        return _cur_stream.write("HTTP/1.1 100 Continue\r\n\r\n");
    }

}

void ServerRequest::reset()
{
    _recv_header_data.clear();
    _recv_header.clear();
    _status = 0;
    _send_state = SendState::status;
    _send_header.clear();
    _output_chunked = false;
    _output_size = {};
    _has_date = false;
    _has_server = false;
    _has_connection= false;
    _has_content_type = false;

}

void ServerRequest::insert_send_header(std::string_view key, std::string_view value) {
    if (_send_header.empty()) {
        _send_header.resize(status_line_reservation);
    }
    _send_header.insert(_send_header.end(), key.begin(),key.end());
    _send_header.push_back(':');
    _send_header.push_back(' ');
    _send_header.insert(_send_header.end(), value.begin(),value.end());
    _send_header.push_back('\r');
    _send_header.push_back('\n');
}

void ServerRequest::set_header(HeaderKey key, std::string_view value) {
    if (key == "Content-Length") {
        if (_output_chunked) return;
        std::size_t sz;
        auto r = std::from_chars(value.data(), value.data()+value.size(), sz, 10);
        if (r.ec != std::errc() || r.ptr != value.data()+value.size()) return;
        _output_size = sz;
    } else  if (key == "Transfer-Encoding") {
        if (HeaderKey(value) == "chunked") {
            if (_output_size.has_value()) return;
            _output_chunked = true;
        }
    } else if (key == "Content-Type") {
        _has_content_type = true;
    } else if (key == "Date") {
        _has_date = true;
    } else if (key == "Server") {
        _has_server = true;
    } else if (key == "Connection") {
        if (HeaderKey(value) == "close") {
            _keep_alive = false;
        }
        if (HeaderKey(value) == "keep-alive" && !_keep_alive) {
            return;
        }
        if (HeaderKey(value) == "upgrade") {
            _upgrade = true;
        }

        _has_connection = true;
    }
    insert_send_header(key, value);
}

void ServerRequest::set_header(HeaderKey key, std::size_t sz) {
    if (key == "Content-Length") {
        if (_output_chunked) return;
        _output_size = sz;
        insert_send_header(key, std::to_string(sz));
    } else {
        set_header(key, std::to_string(sz));
    }
}

void ServerRequest::set_status(unsigned int code) {
    auto msg = response_status_codes[code];
    if (msg.empty()) msg = "No message";
    set_status(code, msg);
}

void ServerRequest::set_status(unsigned int code, std::string_view message) {
    _status = code;
    _status_message = message;
}

void ServerRequest::set_content_type(ContentType ctx) {
    auto s =content_types[ctx];
    if (s.empty()) s = "application/octet-stream";
    set_header("Content-Type", s);
}

void ServerRequest::complete_headers() {
    std::string status_str = std::to_string(_status);
    auto proto = protocols[_protocol];
    auto msg = _status_message;
    auto fixsz = proto.size() + status_str.size()+4;
    auto sz = fixsz +_status_message.size();
    if (sz > status_line_reservation) {
        auto rest = status_line_reservation - fixsz;
        msg = msg.substr(0, rest);
    }
    auto iter = _send_header.begin()+ status_line_reservation - sz;
    auto p = std::copy(proto.begin(), proto.end(), iter);
    *p++ = ' ';
    p = std::copy(status_str.begin(), status_str.end(), p);
    *p++ = ' ';
    p = std::copy(msg.begin(), msg.end(), p);
    *p++ = '\r';
    *p++ = '\n';

    if (((!_output_chunked && !_output_size) || !_keep_alive)&& !_has_connection) {
        set_header("Connection", "close");
    }
    if (!_has_date) {
        set_header("Date","TODO"); //TODO
    }
    if (!_has_server) {
        set_header("Server","TODO"); //TODO
    }
    if (!_has_content_type && !_upgrade
            && !_output_chunked && (!_output_size || *_output_size >  0)) {
        set_header("Content-Type","text/plain;charset=utf-8");
    }

    std::string_view whole_header(_send_header.begin(), _send_header.end());
}

awaitable<bool> ServerRequest::send(std::string_view response_body) {
    if (!_output_size && !_output_chunked) {
        set_header("Content-Length", response_body.size());
    }
    send();//todo;
    //todo
}

awaitable<Stream> ServerRequest::send() {
    complete_headers();
    //todo send headers

}

Stream ServerRequest::prepare_body() {
    if (_has_body) {
        _has_body = false;
        if (_has_body_chunked) {
            return ChunkedStream::create(_cur_stream);
        } else if (_has_body_size) {
            return LimitedStream::create_read_limited(_cur_stream, _has_body_size);
        } else {
            return NullStream::create();
        }
    } else {
        return NullStream::create();
    }
}

}
}
