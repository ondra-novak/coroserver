#include "http_server_request.h"
#include "limited_stream.h"
#include "chunked_stream.h"
#include "http_stringtables.h"

#include <fstream>
namespace coroserver {

namespace http {

std::string ServerRequest::server_name = "CoroServer 1.0 (C++20)";


static constexpr std::size_t status_response_max_len=64;

static constexpr search_kmp search_hdr_sep("\r\n\r\n");


ServerRequest::ServerRequest(Stream s, bool secure)
    :_cur_stream(std::move(s))
    ,_secure(secure)
    ,_body_stream(nullptr)
    {}

ServerRequest::~ServerRequest() {
}

coro::lazy_future<bool> ServerRequest::load() {
    return _target.on_activate<LazyLoadTarget>([&](auto promise) {
        _promise = std::move(promise);
        _status_code = 0;
        _status_message = {};
        _search_hdr_state = 0;
        _body_processed = false;
        _headers_sent = false;
        _header_data.clear();
        _header_data.reserve(256);
        _output_headers.clear();
        _output_headers.reserve(256);
        _output_headers.resize(status_response_max_len);
        _output_headers_summary = {};
        _url_cache.clear();

        auto &t = _target.as<ReadTarget>();
        coro::target_simple_activation(t,[&](auto){load_cycle();});
        _read_fut << [&]{return _cur_stream.read();};
        _read_fut.register_target(t);
    });

}

void ServerRequest::load_cycle() {
    try {
        std::string_view data = _read_fut;
        if (data.empty()) {
            _promise.get<bool>()(false);
            return;
        }
        for (std::size_t cnt = data.size(), i = 0; i < cnt; i++) {
            char c= data[i];
            _header_data.push_back(c);
            if (search_hdr_sep(_search_hdr_state,c)) {
                _cur_stream.put_back(data.substr(i+1));
                _header_data.resize(_header_data.size()-search_hdr_sep.length());
                bool b = parse_request({_header_data.data(), _header_data.size()});
                if (!b) _keep_alive = false;
                _promise.get<bool>()(true);
                return;
            }
        }
        _read_fut << [&]{return _cur_stream.read();};
        _read_fut.register_target(_target.as<ReadTarget>());
        return;

    } catch (...) {
        _promise.get<bool>().reject();
    }
}


bool ServerRequest::parse_request(std::string_view req_header) {

    std::string_view first_line;
    if (!HeaderMap::headers(req_header, _req_headers, first_line)) {
        //failed to load headers, return false
        set_status(400); //BAD REQUEST
        return false;
    }

    auto splt = splitAt(first_line, " ");
    std::string_view str_method = splt();
    std::string_view str_path = splt();
    std::string_view str_vers = splt();
    if (splt || str_method.empty() || str_vers.empty() || str_path.empty()) {
        set_status(400);
        return false;
    }

    _version = strVer[str_vers];
    _method = strMethod[str_method];
    _vpath = _path = str_path;
    if (_method == Method::unknown || _version == Version::unknown) {
        set_status(400);
        return false;
    }

    return parse_headers();

}


bool ServerRequest::parse_headers() {
    using namespace strtable;

    _body_stream = Stream(nullptr);

    auto hv = _req_headers[hdr_host];
    if (hv.has_value())
        _host = hv;
    _keep_alive = (_version == Version::http1_1);

    hv = _req_headers[hdr_connection];
    if (hv.has_value()) {
        if (hv == val_keep_alive) {
            _keep_alive = true;
        } else {
            _keep_alive = false;
        }
    }
    _expect_100_continue = false;
    hv = _req_headers[hdr_expect];
    if (hv.has_value()) {
        if (hv == val_100_continue && _method != Method::GET && _method != Method::HEAD) {
            _expect_100_continue = true;
        } else {
            set_status(417);
            return false;
        }
    }
    _has_body = false;
    if (_method == Method::GET || _method == Method::HEAD) {
        //for GET or HEAD neither content length, nor transfer encoding is allowed
        hv = _req_headers[hdr_content_length];
        if (hv.has_value()) {
            set_status(400);
            return false;
        }
        hv = _req_headers[hdr_transfer_encoding];
        if (hv.has_value()) {
            set_status(400);
            return false;
        }
        //no stream
        return true;
    }
    //other messages allows body
    //if limited stream
    hv = _req_headers[hdr_content_length];
    if (hv.has_value()) {
        std::size_t len = hv.get_uint();
        hv = _req_headers[hdr_transfer_encoding];
        //transfer encoding is not allowed here
        if (hv.has_value()) {
            set_status(400);
            return false;
        }
        if (len) {
            _has_body = true;
            _body_stream = LimitedStream::read(_cur_stream, len);
        }
        return true;
    }
    //check for transfer encoding
    hv = _req_headers[hdr_transfer_encoding];
    if (hv.has_value()) {
        _has_body = true;
        //only chunked is supported
        if (hv == val_chunked) {
            _body_stream = ChunkedStream::read(_cur_stream);
            return true;
        } else {
            set_status(501);
            return false;
        }
    }
    //probably no body
    _body_stream = Stream(nullptr);
    return true;
}

ServerRequest& ServerRequest::set_status(int status) {
    static constexpr std::string_view ukst("Unknown status");
    set_status(status, strStatusMessages.get(status, ukst));
    return *this;
}

ServerRequest& ServerRequest::set_status(int code, const std::string_view &message) {
    _status_code = code;
    _status_message = message;
    return *this;
}

static constexpr std::size_t string2num(std::string_view val) {
    std::size_t a = 0;
    for (char c: val) {
        if (std::isdigit(c)) a = a * 10 + (c - '0');
        else throw std::invalid_argument("Invalid number format");
    }
    return a;
}

void ServerRequest::add_header(const std::string_view &key, const std::string_view &value) {
    strIEqual eq;
    if (key.empty()) [[unlikely]] return;
    bool x = eq(key, strtable::hdr_content_type);;
    _output_headers_summary._has_ctxtp |= x;

    x = eq(key, strtable::hdr_content_length);
    _output_headers_summary._has_ctlen |= x;

    if (x) _output_headers_summary._ctlen = string2num(value);
    x = eq(key, strtable::hdr_transfer_encoding);

    if (x) {
        _output_headers_summary._has_te = true;
        _output_headers_summary._has_te_chunked|=eq(value, strtable::val_chunked);
    }
    x = eq(key, strtable::hdr_connection);
    _output_headers_summary._has_connection |= x;
    if (x) {
        if (eq(value,strtable::val_keep_alive)) _keep_alive = true;
        else _keep_alive = false;
    }
    x = eq(key, strtable::hdr_date);
    _output_headers_summary._has_date |= x;
    x = eq(key, strtable::hdr_server);
    _output_headers_summary._has_server |= x;

    std::copy(key.begin(), key.end(), std::back_inserter(_output_headers));
    _output_headers.push_back(':');
    _output_headers.push_back(' ');
    std::copy(value.begin(), value.end(), std::back_inserter(_output_headers));
    _output_headers.push_back('\r');
    _output_headers.push_back('\n');
}

void ServerRequest::add_header(const std::string_view &key, const std::size_t &value) {
    add_header(key, std::to_string(value));
}

void ServerRequest::add_header(const std::string_view &key, const std::chrono::system_clock::time_point &tp) {
    httpDate(std::chrono::system_clock::to_time_t(tp), [&](std::string_view txt){
       add_header(key, txt);
    });
}

ServerRequest& ServerRequest::content_type(ContentType ct) {
    add_header(strtable::hdr_content_type, strContentType[ct]);
    return *this;
}

ServerRequest& ServerRequest::no_buffering() {
    add_header(strtable::hdr_x_accel_buffering, "no");
    return *this;
}

ServerRequest& ServerRequest::caching(std::size_t seconds) {
    if (seconds == 0) {
        add_header(strtable::hdr_cache_control,"no-store, no-cache, max-age=0, must-revalidate, proxy-revalidate");
    } else {
        std::string wrkbuff("max-age=");
        wrkbuff.append(std::to_string(seconds));
        add_header(strtable::hdr_cache_control, wrkbuff);
    }
    return *this;
}

ServerRequest& ServerRequest::location(const std::string_view &location) {
    add_header(strtable::hdr_location, location);
    return *this;
}

ServerRequest& ServerRequest::last_modified(std::chrono::system_clock::time_point tp) {
    add_header(strtable::hdr_last_modified, tp);
    return *this;
}
ServerRequest& ServerRequest::add_date(std::chrono::system_clock::time_point tp) {
    add_header(strtable::hdr_date, tp);
    return *this;
}

ServerRequest& ServerRequest::operator ()(const std::string_view &key, const std::string_view &value) {
    add_header(key,value);
    return *this;
}
ServerRequest& ServerRequest::operator ()(const std::string_view &key, const std::size_t &value) {
    add_header(key,value);
    return *this;
}
ServerRequest& ServerRequest::operator ()(const std::string_view &key, const std::chrono::system_clock::time_point &value) {
    add_header(key,value);
    return *this;
}

bool ServerRequest::allow(const std::initializer_list<Method> &methods) {
    if (std::find(methods.begin(), methods.end(), _method) != methods.end()) return true;
    std::string lst;
    for(auto iter = methods.begin(); iter != methods.end(); ++iter) {
        if (iter != methods.begin()) lst.append(", ");
        lst.append(strMethod[*iter]);
    }
    add_header(strtable::hdr_allow, lst);
    set_status(405);
    return false;
}

std::string_view ServerRequest::get_url() const {
    if (_url_cache.empty()) {
        bool secure = _secure;
        if (!secure) {
            ForwardedHeader fwhdr(_req_headers[strtable::hdr_forwarded]);
            secure = fwhdr.proto == "https";
        }
        bool ws =false;
        auto wshdr = operator[](strtable::hdr_upgrade);
        if (wshdr.has_value()) {
            strIEqual eq;
            if (eq(wshdr, strtable::val_websocket)) ws = true;
        }
        std::string_view p = ws?(secure?"wss":"ws"):(secure?"https":"http");
        std::string_view h = get_host();
        std::string_view u = get_path();
        _url_cache.reserve(p.size()+h.size()+u.size()+3);
        _url_cache.append(p);
        _url_cache.append("://");
        _url_cache.append(h);
        _url_cache.append(u);
    }
    return _url_cache;

}

bool ServerRequest::headers_sent() {
    return _headers_sent;
}

std::string ServerRequest::url_decode(const std::string_view &str) {
    std::string out;
    out.reserve(str.size()*3/2);
    url::decode(str, [&](char c){out.push_back(c);});
    return out;
}

void ServerRequest::clear_headers() {
    _output_headers.clear();
    _status_code = 0;
    _status_message = {};
}

void ServerRequest::content_type_from_extension(const std::string_view &path) {
    auto pos = path.rfind(".");
    if (pos != path.npos) {
        auto ex = path.substr(pos+1);
        ContentType ct = extensionToContentType(ex);
        content_type(ct);
    } else {
        content_type(ContentType::binary);
    }
}


coro::future<bool> ServerRequest::send(std::string &&body) {
    _user_buffer = std::move(body);
    _send_body_data = _user_buffer;
    add_header(strtable::hdr_content_length, body.size());
    return [&](auto prom) {
        _promise = std::move(prom);
        if (_has_body && !_expect_100_continue) {
            _read_fut << [&]{return _body_stream.read();};
            _read_fut.register_target(
                    _target.on_activate<ReadTarget>(
                            [&](auto) {
                send_discard_body<&ServerRequest::send_body_continue>();
            }));
            return;
        }
        send_body_continue();
    };
}

coro::future<bool> ServerRequest::send(std::ostringstream &body) {
    return send(body.str());
}

template<auto cont>
void ServerRequest::send_discard_body() {
    try {
        std::string_view data = _read_fut;
        if (data.empty()) {
            (this->*cont)();
        } else {
            _read_fut << [&]{return _body_stream.read();};
            _read_fut.register_target(_target.as<ReadTarget>());
        }
    } catch (...) {
        _promise.get<Stream>().reject();
    }
}

void ServerRequest::send_continue() {
    if (!_headers_sent) {
        _headers_sent = true;
        _write_fut << [&]{return _cur_stream.write(prepare_output_headers());};
        _write_fut.register_target(_target.on_activate<WriteTarget>(
                [&](auto f) {
                    try {
                        bool b = f;
                        auto &res = _promise.get<Stream>();
                        if (!b) {
                            res(LimitedStream::write(_cur_stream, 0));
                        } else {
                            if (_output_headers_summary._has_te && _output_headers_summary._has_te_chunked) {
                                res(ChunkedStream::write(_cur_stream));
                            } else if (_output_headers_summary._has_ctlen) {
                                res(LimitedStream::write(_cur_stream, _output_headers_summary._ctlen));
                            } else {
                                res(_cur_stream);
                            }
                        }
                    } catch (...) {
                        _promise.get<Stream>().reject();
                    }
                }
        ));
    }
}

void ServerRequest::send_body_continue() {
    if (!_headers_sent) {
        _headers_sent = true;
        _write_fut << [&]{return _cur_stream.write(prepare_output_headers());};
        _write_fut.register_target(_target.on_activate<WriteTarget>(
                [&](auto f) {
                    try {
                        bool b = f;
                        auto &res = _promise.get<bool>();
                        if (!b) {
                            res(false);
                        } else {
                            _write_fut << [&]{return _cur_stream.write(_send_body_data);};
                            _write_fut.register_target(_target.on_activate<WriteTarget>(
                                    [&](auto f) {
                                            auto &res = _promise.get<bool>();
                                            try {
                                                res(f->get());
                                            } catch (...) {
                                                res.reject();
                                            }
                            }));
                        }
                    } catch (...) {
                        _promise.get<Stream>().reject();
                    }
                }
        ));
    }
}

coro::future<Stream> ServerRequest::send() {
    return [&](auto prom) {
        _promise = std::move(prom);
        if (_has_body && !_expect_100_continue) {
            _read_fut << [&]{return _body_stream.read();};
            _read_fut.register_target(
                    _target.on_activate<ReadTarget>(
                            [&](auto) {
                send_discard_body<&ServerRequest::send_continue>();
            }));
            return;
        }
        send_continue();
    };
}

std::string_view ServerRequest::prepare_output_headers() {
    if (_status_code == 0) {
        set_status(200);
    }
    if (!_output_headers_summary._has_server) {
        add_header(strtable::hdr_server, server_name);
    }
    if (!_output_headers_summary._has_date) {
        add_date(std::chrono::system_clock::now());
    }
    if (!_output_headers_summary._has_ctxtp) {
        add_header(strtable::hdr_content_type, strContentType[ContentType::binary]);
    }
    //neither te, no ctxlen set
    if (!_output_headers_summary._has_te && !_output_headers_summary._has_ctlen) {
        if (_keep_alive) {
            add_header(strtable::hdr_transfer_encoding, strtable::val_chunked);
        }
    }
    if (!_keep_alive && !_output_headers_summary._has_connection) {
        add_header(strtable::hdr_connection, strtable::val_close);
    }
    auto ver = strVer[_version];
    auto status = std::to_string(_status_code);
    auto msg=_status_message;
    std::size_t needsz = ver.size() + status.size()+_status_message.size()+4;
    if (needsz>status_response_max_len) {
        msg=msg.substr(0, status_response_max_len-4-ver.size() - status.size());
        needsz =status_response_max_len;
    }
    _output_headers.push_back('\r');
    _output_headers.push_back('\n');

    char *itr=_output_headers.data()+status_response_max_len-needsz;
    itr = std::copy(ver.begin(), ver.end(), itr);
    *itr++=' ';
    itr = std::copy(status.begin(),status.end(), itr);
    *itr++=' ';
    itr = std::copy(msg.begin(), msg.end(), itr);
    *itr++='\r';
    *itr++='\n';

    return std::string_view (_output_headers.data(), _output_headers.size())
              .substr(status_response_max_len-needsz);

}


coro::lazy_future<Stream> ServerRequest::get_body() {
    return _target.on_activate<LazyGetStreamTarget>([&](auto promise) {
        if (!_has_body) {
            promise(LimitedStream::read(_cur_stream, 0));
            return;
        }
        _has_body = false;
        if (_expect_100_continue) {
            _expect_100_continue = false;
            auto iter = _output_headers.begin();
            auto ver = strVer[_version];
            std::string_view txt(" 100 Continue\r\n\r\n");
            iter = std::copy(ver.begin(), ver.end(), iter);
            iter = std::copy(txt.begin(), txt.end(), iter);
            std::string_view out(_output_headers.data(), std::distance(_output_headers.begin(), iter));
            _promise = std::move(promise);
            _write_fut << [&]{return _cur_stream.write(out);};
            _write_fut.register_target(_target.on_activate<WriteTarget>([&](auto f){
                try {
                    bool b = *f;
                    if (b) _promise.get<Stream>()(_body_stream);
                    else _promise.get<Stream>()(LimitedStream::read(_cur_stream, 0));
                } catch (...) {
                    _promise.get<Stream>().reject();
                }
            }));
            return;
        }
        promise(_body_stream);
    });
}

coro::future<bool> ServerRequest::send_file(const std::string &path, bool use_chunked) {
    std::ifstream f(path);
    if (!f) return false;
    if (!use_chunked) {
        f.seekg(0,std::ios::end);
        auto sz = f.tellg();
        f.seekg(0,std::ios::beg);
        add_header(strtable::hdr_content_length, std::size_t(sz));
    }
    return send_stream(std::move(f));
}



}

}
