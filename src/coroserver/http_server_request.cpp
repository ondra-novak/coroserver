#include "http_server_request.h"
#include "limited_stream.h"
#include "chunked_stream.h"

#include <fstream>
namespace coroserver {

namespace http {

std::string ServerRequest::server_name = "CoroServer 1.0 (C++20)";

static constexpr std::string_view header_separator("\r\n\r\n");
static constexpr std::size_t status_response_max_len=64;

static std::atomic<std::size_t> server_request_coro_size(0);

ServerRequest::ServerRequest(Stream s)
    :_coro_storage(server_request_coro_size)
    ,_cur_stream(std::move(s))
    ,_body_stream(nullptr)
    {}

ServerRequest::~ServerRequest() {
}

cocls::future<bool> ServerRequest::load() {
    _header_data.clear();
    _status_code = 0;
    _body_processed = false;
    _headers_sent = false;
    _output_headers.clear();
    _output_headers.reserve(256);
    _output_headers.resize(status_response_max_len);
    _output_headers_summary = {};
    return load_coro(_coro_storage);
}

template<typename Alloc>
cocls::with_allocator<Alloc, cocls::async<bool> > ServerRequest::load_coro(Alloc &a) {
    bool b = co_await _cur_stream.read_until(a, _header_data, "\r\n\r\n", 65536);
    if (!b) co_return b;
    b = parse_request({_header_data.data(), _header_data.size()});
    if (!b) _keep_alive = false;
    co_return b;
}


bool ServerRequest::parse_request(std::string_view req_header) {

    std::string_view first_line;
    if (!HeaderMap::headers(req_header, _req_headers, first_line)) {
        //failed to load headers, return false
        _status_code = 400; //BAD REQUEST
        return false;
    }

    auto splt = splitAt(first_line, " ");
    std::string_view str_method = splt();
    std::string_view str_path = splt();
    std::string_view str_vers = splt();
    if (splt || str_method.empty() || str_vers.empty() || str_path.empty()) {
        _status_code = 400;
        return false;
    }

    _version = strVer(str_vers);
    _method = strMethod(str_method);
    _path = str_path;
    if (_method == Method::unknown || _version == Version::unknown) {
        _status_code = 400;
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
            _status_code = 417;
            return false;
        }
    }
    _has_body = false;
    if (_method == Method::GET || _method == Method::HEAD) {
        //for GET or HEAD neither content length, nor transfer encoding is allowed
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
            _status_code = 400;
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
            _status_code = 501;
            return false;
        }
    }
    //probably no body
    _body_stream = Stream(nullptr);
    return true;
}

ServerRequest& ServerRequest::set_status(int status) {
    set_status(status, StatusCodeMap::instance.message(status));
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
    add_header(strtable::hdr_content_type, strContentType(ct));
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
        lst.append(strMethod(*iter));
    }
    add_header(strtable::hdr_allow, lst);
    set_status(405);
    return false;
}

std::string ServerRequest::get_url(bool secure) const {
    std::string ret;
    bool ws =false;
    auto wshdr = operator[](strtable::hdr_upgrade);
    if (wshdr.has_value()) {
        strIEqual eq;
        if (eq(wshdr, strtable::val_websocket)) ws = true;
    }
    std::string_view p = ws?(secure?"wss":"ws"):(secure?"https":"http");
    std::string_view h = get_host();
    std::string_view u = get_path();
    ret.reserve(p.size()+h.size()+u.size()+3);
    ret.append(p);
    ret.append("://");
    ret.append(h);
    ret.append(u);
    return ret;

}

bool ServerRequest::headers_sent() {
    return _headers_sent;
}

void ServerRequest::url_decode(const std::string_view &str, std::string &out) {
    for (auto iter = str.begin(); iter != str.end(); ++iter) {
        if (*iter == '%') {
            ++iter;
            if (iter != str.end()) {
                char c[2];
                c[0] = *iter;
                ++iter;
                if (iter != str.end()) {
                    c[1] = *iter;
                    int z = 32;
                    std::from_chars(c, c+2, z, 16);
                    out.push_back(static_cast<char>(z));
                } else {
                    break;
                }
            } else {
                break;
            }
        } else {
            out.push_back(*iter);
        }
    }
}
std::string ServerRequest::url_decode(const std::string_view &str) {
    std::string out;
    out.reserve(str.size()*3/2);
    url_decode(str, out);
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

cocls::future<void> ServerRequest::send(std::string_view body) {
    return send_coro(_coro_storage, body);
}

template<typename Alloc>
cocls::with_allocator<Alloc, cocls::async<void> > ServerRequest::send_coro(Alloc &a, std::string_view body) {
    add_header(strtable::hdr_content_length, body.size());
    Stream s = co_await send_coro(a);
    co_await s.write(body);
    co_await s.write_eof();
}


cocls::future<void> ServerRequest::send(ContentType ct, std::string_view body) {
    content_type(ct);
    return send(body);
}

cocls::future<Stream> ServerRequest::send() {
    return send_coro(_coro_storage);
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
        add_header(strtable::hdr_content_type, "text/plain; charset=utf-8");
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
    auto ver = strVer(_version);
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

template<typename Alloc>
cocls::with_allocator<Alloc, cocls::async<Stream> > ServerRequest::send_coro(Alloc &a) {

    co_await discard_body_coro(a);


    co_await _cur_stream.write(prepare_output_headers());

    _headers_sent = true;

    if (_output_headers_summary._has_te && _output_headers_summary._has_te_chunked) {
        co_return ChunkedStream::write(_cur_stream);
    } else if (_output_headers_summary._has_ctlen) {
        co_return  LimitedStream::write(_cur_stream, _output_headers_summary._ctlen);
    } else {
        co_return _cur_stream;
    }

}

template<typename Alloc>
cocls::with_allocator<Alloc, cocls::async<void> > ServerRequest::discard_body_coro(Alloc &a) {
    if (!_has_body || _expect_100_continue) {
        _has_body = false;
        _expect_100_continue = false;
        co_return;
    }
    co_await _body_stream.discard(a);
}




cocls::future<bool> ServerRequest::send_file(const std::string &path, bool use_chunked) {
    std::ifstream f(path);
    if (!f) co_return false;
    if (!use_chunked) {
        f.seekg(0,std::ios::end);
        auto sz = f.tellg();
        f.seekg(0,std::ios::beg);
        add_header(strtable::hdr_content_length, std::size_t(sz));
    }
    Stream s = co_await send();
    char buff[16384];
    while (!f.eof()) {
        f.read(buff, sizeof(buff));
        std::size_t sz = f.gcount();
        if (sz) {
            bool b = co_await s.write(std::string_view(buff,sz));
            if (!b) break;
        } else {
            break;
        }
    }
    co_await s.write_eof();
    co_return true;


}

cocls::future<void> ServerRequest::discard_body() {
    return discard_body_coro(_coro_storage);
}


template<typename Alloc>
cocls::with_allocator<Alloc, cocls::async<Stream> > ServerRequest::get_body_coro(Alloc &) {
    char buff[256];
    auto ver = strVer(_version);
    std::string_view txt(" 100 Continue\r\n\r\n");
    auto iter = std::copy(ver.begin(), ver.end(), std::begin(buff));
    iter = std::copy(txt.begin(), txt.end(), iter);
    std::string_view out(buff, std::distance(std::begin(buff), iter));
    if (!co_await _cur_stream.write(out)) {
        co_return Stream(LimitedStream::read(_cur_stream,0));
    } else {
        co_return _body_stream;
    }
}


cocls::future<Stream> ServerRequest::get_body() {
    if (!_has_body) {
        return cocls::future<Stream>::set_value(LimitedStream::read(_cur_stream, 0));
    }
    if (_expect_100_continue) {
        return get_body_coro(_coro_storage);
    } 
    return cocls::future<Stream>::set_value(_body_stream);
}



}

}
