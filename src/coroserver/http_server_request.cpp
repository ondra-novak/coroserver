#include "http_server_request.hpp"

namespace coroserver {

HttpServerRequest::HttpServerRequest(Stream s):_s(std::move(s))
{
}


coro::awaitable<bool> HttpServerRequest::load()
{
    reset();
    return [this](coro::awaitable<bool>::result r) {
        _read_until_callback.await(_s.read_until(_recv_header_data, header_separator, max_header_size),
        [this, r = std::move(r)](auto &awt) mutable -> coro::prepared_coro{
            try {
                if (!awt.has_value()) return r(false);
                bool st = awt.await_resume();
                if (!st) return r(false);
                auto pst = parse_header();
                if (pst == ParseHeaderStatus::rejected) {
                    //todo reject generate response
                    return {};
                }
                return r(pst == ParseHeaderStatus::ok);
            } catch (...) {
                return r.set_exception(std::current_exception());
            }
        });
    };
}
void HttpServerRequest::reset()
{
    _recv_header_data.clear();
    _recv_header.clear();
}

HttpServerRequest::ParseHeaderStatus HttpServerRequest::parse_header()
{
    std::string_view data(_recv_header_data.data(), _recv_header_data.size());
    auto first_line = trim(split_at(data, "\r\n"));
    while (data.empty()) {
        auto ln = split_at(data, "\r\n");
        if (ln.empty()) continue;
        auto k = trim(split_at(ln,":"));
        auto v = trim(ln);
        _recv_header.emplace_back(k,v);
    }
    _method = split_at(first_line, " ");
    _path = split_at(first_line, " ");
    _protocol = first_line;

    return ParseHeaderStatus::ok;    

}
}