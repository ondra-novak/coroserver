/*
 * http_static_page.cpp
 *
 *  Created on: 18. 11. 2022
 *      Author: ondra
 */


#include "http_static_page.h"

#include <fstream>

namespace coroserver {

namespace http {

StaticPage::StaticPage(std::filesystem::path document_root, std::string index_html, unsigned int cache_seconds)
:_doc_root(document_root)
,_index_html(index_html)
,_cache_seconds(cache_seconds)
{
}

cocls::future<bool> StaticPage::operator ()(ServerRequest &req, std::string_view path) const {
    auto p = _doc_root;
    {
        bool addindex = true;
        auto splt = split_path(path);
        std::string_view part = splt();
        while (!part.empty()) {
            if (part == "..") return cocls::future<bool>::set_value();
            if (part != ".") {
                p/=part;
                addindex = false;
            }
            part = splt();
        }
        if (addindex) p/=_index_html;
    }

    std::error_code ec;
    auto wt = std::filesystem::last_write_time(p, ec);
    if (ec) {
        return cocls::future<bool>::set_value();
    }

    auto etag = "\""+ std::to_string(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                        wt.time_since_epoch()).count()) + "\"";

    HeaderValue nonem = req[strtable::hdr_if_none_match];
    if (nonem.has_value()) {
        std::string_view nn(nonem);
        if (nn.find(etag) != nn.npos) {
            req.set_status(304);
            return req.send("");
        }
    }

    std::string fp=p;

    std::ifstream in(fp);
    if (!in) return cocls::future<bool>::set_value(false);

    in.seekg(0, std::ios::end);
    auto sz = in.tellg();
    in.seekg(0);

    req(strtable::hdr_etag, etag);
    if (_cache_seconds) req.caching(_cache_seconds);
    req.content_type_from_extension(fp);
    req(strtable::hdr_content_length, std::size_t(sz));
    return req.send_stream(std::move(in));







}

}


}

