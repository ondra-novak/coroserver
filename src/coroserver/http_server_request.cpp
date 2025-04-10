#include "http_server_request.hpp"

#include "null_stream.h"

#include "limited_stream.hpp"

#include "chunked_stream.hpp"
#include <charconv>
#include <ctime>
#include <fstream>
using coroserver::LimitedStream;
namespace coroserver {

namespace http {

std::string_view ServerRequest::default_server_name = "httpd";


ServerRequest::ServerRequest(Stream s, std::string_view server_name, ConnectionType con_type)
        :_cur_stream(s)
        , _server_name(server_name.empty()?default_server_name:server_name)
        ,_con_type(con_type)
        {}




awaitable<bool> ServerRequest::parse() {
    //to load request, _keep_alive must be true
    if (!is_keep_alive()) return false;
    reset();
    auto awt = _cur_stream.read_until(_recv_header_data, header_separator, max_header_size);
    if (awt.is_ready()) {
        return  parse2();
    } else {
        _read_until_callback.set_awaiter(std::move(awt));
        return [this](coro::awaitable<bool>::result r) mutable {
            if (!r) {
                _read_until_callback.get_awaiter().cancel();
                return r.set_empty();
            }
            return _read_until_callback.await([this, r = std::move(r)](auto &awt) mutable {
                try {
                    if (!awt.has_value()) return r.set_empty();
                    bool st = awt.await_resume();
                    if (!st) return r.set_empty();
                    return r(parse2());
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

bool ServerRequest::parse2() {
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

    if (get_host().empty()) return false;

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
    return true;
}

awaitable<Stream> ServerRequest::get_body() {
    _touched = true;
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

    _send_header.clear();
    _output_chunked = false;
    _output_size = {};
    _has_date = false;
    _has_server = false;
    _has_connection= false;
    _has_content_type = false;

    _touched = false;
    _headers_sent = false;

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
    _headers_sent = true;
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

void ServerRequest::set_content_length(std::size_t len) {
    set_header("Content-Length", len);
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

constexpr const char *tab_day_of_week[7] = {
        "Sun","Mon","Tue","Wed","Thu","Fri","Sat"
};
constexpr const char *tab_month[12] = {
        "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
};

void ServerRequest::set_header_date_rfc5322(HeaderKey key, std::time_t t) {
    char date_buffer[60] = {};
    std::tm tm = *std::gmtime(&t);

    auto m = tab_day_of_week[tm.tm_mon];
    auto d = tab_month[tm.tm_wday];

    snprintf(date_buffer, sizeof(date_buffer)-1, "%s, %d %s %d %2d:%2d:%2d GMT",
            d, tm.tm_mday, m, tm.tm_year+1900, tm.tm_hour, tm.tm_min, tm.tm_sec);
    set_header(key, date_buffer);
}



std::string_view  ServerRequest::complete_headers() {
    if (_status == 0) _status = 200;
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
        set_header_date_rfc5322("Date", std::time(nullptr));
    }
    if (!_has_server) {
        set_header("Server",_server_name); //TODO
    }
    if (!_has_content_type && !_upgrade
            && !_output_chunked && (!_output_size || *_output_size >  0)) {
        set_header("Content-Type","text/plain;charset=utf-8");
    }

    _send_header.push_back('\r');
    _send_header.push_back('\n');

    return std::string_view(_send_header.begin(), _send_header.end());
}

template<typename ... Args>
auto ServerRequest::send_helper(Args  ... args) {

    constexpr bool send_body = sizeof...(args) == 1;
    using AwtType = std::conditional_t<sizeof...(Args) == 1, awaitable<bool>, awaitable<Stream> >;
    using ResultType = AwtType::result;

    auto hdrs = complete_headers();
    auto awt = _cur_stream.write(hdrs);
    if (awt.await_ready()) {
        if (awt.await_resume()) {
            if constexpr(send_body) {
                return AwtType(finish_send().write(args...));
            } else {
                return AwtType(finish_send());
            }
        } else {
            _keep_alive = false;
            if constexpr(send_body) {
                return AwtType(false);
            } else {
                return AwtType(NullStream::create());
            }
        }
    } else {
        _send_callback.set_awaiter(std::move(awt));
        return AwtType([this, args...](ResultType r) {
            if (!r) {
                _send_callback.get_awaiter().cancel();
                return r.set_empty();
            }
            return _send_callback.await([this, r = std::move(r), args...](auto &awt) mutable {
                try {
                    if (awt.has_value() && awt.await_resume()) {
                        if constexpr(send_body) {
                            auto awt2 = finish_send().write(args...);
                            return awt2.forward(r);
                        } else {
                            return r(finish_send());
                        }
                    } else {
                        _keep_alive = false;
                        if constexpr(send_body) {
                            return r(false);
                        } else {
                            return r(NullStream::create());
                        }
                    }
                    return r.set_value(NullStream::create());
                } catch (...) {
                    return r.set_exception(std::current_exception());
                }

            });
        });
    }

}

awaitable<bool> ServerRequest::send(std::string_view response_body) {
    if (!_output_size && !_output_chunked) {
        set_content_length(response_body.size());
    }
    return send_helper(response_body);
}

awaitable<Stream> ServerRequest::send() {
    return send_helper();
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
Stream ServerRequest::finish_send() {
    if (_output_chunked) return ChunkedStream::create(_cur_stream);
    else if (_output_size) {
        if (*_output_size == 0) return NullStream::create();
        return LimitedStream::create_write_limited(_cur_stream, *_output_size);
    }
    else {
        return _cur_stream;
    }
}

awaitable<bool> ServerRequest::send_file(const std::filesystem::path &p) {
    auto exs = p.extension().string();
    auto ex = std::string_view(exs.data()+1, exs.size()-1);
    return send_file(p, content_types_to_extension[ex]);


}
awaitable<bool> ServerRequest::send_file(const std::filesystem::path &p, ContentType ctx) {
    std::error_code ec;
    if (!std::filesystem::exists(p,ec) || !std::filesystem::is_regular_file(p,ec)) {
        return false;
    }
    if (ec != std::error_code{}) return false;
    auto sz = std::filesystem::file_size(p, ec);
    if (ec != std::error_code{}) return false;

    set_content_type(ctx);
    set_content_length(sz);
    std::ifstream f(p, std::ios::binary|std::ios::in);
    if (!f) return false;

    auto coro = [this](std::ifstream f) -> awaitable<bool>{
        char buff[8192];
        auto awt = send();
        co_await awt.ready();
        if (!awt.has_value()) co_return false;

        Stream s = awt;

        while (true) {
            f.read(buff, sizeof(buff));
            auto sz = f.gcount();
            if (sz == 0) co_return true;
            bool b = co_await s.write(std::string_view(buff, sz));
            if (!b) co_return false;
        }
    };
    return coro(std::move(f));
}

awaitable<bool> ServerRequest::send(std::ostringstream &stream) {
    _tmp_body = std::move(stream).str();
    return send(_tmp_body);

}

awaitable<bool> ServerRequest::send_error() {
    std::ostringstream buff;

    buff << "<!DOCTYPE html>"
           "<html><head><title>"
            << _status << " " << _status_message <<
            "</title>"
            "</head>"
            "<body>"
            "<h1>" << _status << " " << _status_message << "</h1>"
            "</body>"
            "</html>";

    set_content_type(ContentType::html);
    return send(buff);
}

ServerRequest::State ServerRequest::get_state() const {
    if (_headers_sent) return headers_sent;
    if (!_send_header.empty()) return headers_prepared;
    if (_touched) return body_read;
    return untouched;
}

bool ServerRequest::is_secure() const {
    switch (_con_type) {
        default:
        case ConnectionType::direct_unsecure: return false;
        case ConnectionType::direct_secure: return true;
        case ConnectionType::reverse_proxy: {
            auto v = get_header("X-Forwarded-Proto");
            return (v.has_value() && HeaderKey(*v) == "https");
        }
    }
}
///Retrieve prefix for mapping paths
std::string_view ServerRequest::get_path_prefix() const {
    if (_con_type != ConnectionType::reverse_proxy) return {};
    auto r = get_header("X-Forwarded-Prefix");
    if (!r.has_value()) return {};
    return *r;
}

std::string_view ServerRequest::get_host() const {
    auto r = get_header("Host");
    if (r.has_value()) return *r;
    return {};
}



awaitable<bool> ServerRequest::redirect(std::string_view uri,RedirectType type) {
    if (uri.find("..") != uri.npos) {
        return redirect(normalize_uri(uri), type);
    }
    std::ostringstream loc;
    if (is_secure()) loc << "https"; else loc << "http";
    loc << "://" << get_host() << get_path_prefix() << uri;
    set_status(static_cast<unsigned int>(type));
    set_header("Location", loc.view());
    set_content_type(ContentType::octet_stream);
    return send("");
}


}
}
