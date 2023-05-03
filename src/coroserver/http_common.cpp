#include "http_common.h"
#include "strutils.h"
#include <algorithm>

namespace coroserver {

namespace http {

namespace strtable {


std::string_view hdr_allow("Allow");
std::string_view hdr_accept("Accept");
std::string_view hdr_access_control_request_method("Access-Control-Request-Method");
std::string_view hdr_access_control_request_headers("Access-Control-Request-Headers");
std::string_view hdr_authorization("Authorization");
std::string_view val_basic("basic");
std::string_view val_bearer("bearer");
std::string_view hdr_cache_control("Cache-Control");
std::string_view hdr_host("Host");
std::string_view hdr_content_type("Content-Type");
std::string_view hdr_content_length("Content-Length");
std::string_view hdr_transfer_encoding("Transfer-Encoding");
std::string_view val_chunked("chunked");
std::string_view hdr_connection("Connection");
std::string_view val_close("close");
std::string_view val_keep_alive("keep-alive");
std::string_view val_upgrade("upgrade");
std::string_view hdr_set_cookie("Set-Cookie");
std::string_view hdr_sep("\r\n");
std::string_view hdr_date("Date");
std::string_view hdr_server("Server");
std::string_view hdr_cookie("Cookie");
std::string_view hdr_expect("Expect");
std::string_view val_100_continue("100-continue");
std::string_view hdr_etag("ETag");
std::string_view hdr_if_none_match("If-None-Match");
std::string_view hdr_last_modified("Last-Modified");
std::string_view hdr_origin("Origin");
std::string_view hdr_pragma("Pragma");
std::string_view hdr_user_agent("User-Agent");
std::string_view hdr_x_forwarded_for("X-Forwarded-For");
std::string_view hdr_forwarded("Forwarded");
std::string_view hdr_x_forwarded_host("X-Forwarded-Host");
std::string_view hdr_x_forwarded_proto("X-Forwarded-Proto");
std::string_view hdr_x_forwarded_prefix("X-Forwarded-Prefix");
std::string_view hdr_front_end_https("Front-End-Https");
std::string_view hdr_location("Location");
std::string_view hdr_upgrade("Upgrade");
std::string_view val_websocket("websocket");
std::string_view hdr_www_authenticate("WWW-Authenticate");
std::string_view hdr_refresh("Refresh");
std::string_view hdr_x_accel_buffering("X-Accel-Buffering");


}



StaticLookupTable<int, std::string_view, 63> strStatusMessages({
        {100, "Continue"},
        {101, "Switching Protocols"},
        {102, "Processing"},
        {103, "Early Hints"},
        {200, "OK"},
        {201, "Created"},
        {202, "Accepted"},
        {203, "Non-Authoritative Information"},
        {204, "No Content"},
        {205, "Reset Content"},
        {206, "Partial Content"},
        {207, "Multi-Status"},
        {208, "Already Reported"},
        {226, "IM Used"},
        {300, "Multiple Choices"},
        {301, "Moved Permanently"},
        {302, "Found"},
        {303, "See Other"},
        {304, "Not Modified"},
        {305, "Use Proxy"},
        {306, "Switch Proxy"},
        {307, "Temporary Redirect"},
        {308, "Permanent Redirect"},
        {400, "Bad Request"},
        {401, "Unauthorized"},
        {402, "Payment Required"},
        {403, "Forbidden"},
        {404, "Not Found"},
        {405, "Method Not Allowed"},
        {406, "Not Acceptable"},
        {407, "Proxy Authentication Required"},
        {408, "Request Timeout"},
        {409, "Conflict"},
        {410, "Gone"},
        {411, "Length Required"},
        {412, "Precondition Failed"},
        {413, "Payload Too Large"},
        {414, "URI Too Long"},
        {415, "Unsupported Media Type"},
        {416, "Range Not Satisfiable"},
        {417, "Expectation Failed"},
        {418, "I'm a teapot"},
        {421, "Misdirected Request"},
        {422, "Unprocessable Entity"},
        {423, "Locked"},
        {424, "Failed Dependency"},
        {425, "Too Early"},
        {426, "Upgrade Required"},
        {428, "Precondition Required"},
        {429, "Too Many Requests"},
        {431, "Request Header Fields Too Large"},
        {451, "Unavailable For Legal Reasons"},
        {500, "Internal Server Error"},
        {501, "Not Implemented"},
        {502, "Bad Gateway"},
        {503, "Service Unavailable"},
        {504, "Gateway Timeout"},
        {505, "HTTP Version Not Supported"},
        {506, "Variant Also Negotiates"},
        {507, "Insufficient Storage"},
        {508, "Loop Detected"},
        {510, "Not Extended"},
        {511, "Network Authentication Required"}
});



StaticLookupTable<Method, std::string_view, 10> strMethod({
    {Method::GET,"GET"},
    {Method::HEAD,"HEAD"},
    {Method::POST,"POST"},
    {Method::PUT,"PUT"},
    {Method::DELETE,"DELETE"},
    {Method::CONNECT,"CONNECT"},
    {Method::OPTIONS,"OPTIONS"},
    {Method::TRACE,"TRACE"},
    {Method::PATCH,"PATCH"},
    {Method::unknown,"unknown"}
});

StaticLookupTable<Version, std::string_view, 3> strVer({
    {Version::http1_0, "HTTP/1.0"},
    {Version::http1_1, "HTTP/1.1"},
    {Version::unknown, "unknown"}
});

StaticLookupTable<ContentType, std::string_view, 48> strContentType ({
    {ContentType::binary,"application/octet-stream"},
    {ContentType::audio_aac,               "audio/aac"},
    {ContentType::audio_midi,              "audio/midi"},
    {ContentType::audio_mpeg,              "audio/mpeg"},
    {ContentType::audio_mp4,               "audio/mp4"},
    {ContentType::audio_ogg,               "audio/ogg"},
    {ContentType::audio_opus,              "audio/opus"},
    {ContentType::audio_wav,               "audio/wav"},
    {ContentType::text_plain,              "text/plain"},
    {ContentType::text_plain_utf8,         "text/plain;charset=utf-8"},
    {ContentType::text_html,               "text/html"},
    {ContentType::text_html_utf8,          "text/html;charset=utf-8"},
    {ContentType::text_css,                "text/css"},
    {ContentType::text_csv,                "text/csv"},
    {ContentType::text_javascript,         "text/javascript"},
    {ContentType::event_stream,            "text/event-stream"},
    {ContentType::image_avif,              "image/avif"},
    {ContentType::image_bmp,               "image/bmp"},
    {ContentType::image_gif,               "image/gif"},
    {ContentType::image_ico,               "image/vnd.microsoft.icon"},
    {ContentType::image_png,               "image/png"},
    {ContentType::image_svg,               "image/svg+xml"},
    {ContentType::image_tiff,              "image/tiff"},
    {ContentType::image_jpeg,              "image/jpeg"},
    {ContentType::font_otf,                "font/otf"},
    {ContentType::font_ttf,                "font/ttf"},
    {ContentType::font_woff,               "font/woff"},
    {ContentType::font_woff2,              "font/woff2"},
    {ContentType::video_avi,               "video/x-msvideo"},
    {ContentType::video_mpeg,              "video/mpeg"},
    {ContentType::video_ogg,               "video/ogg"},
    {ContentType::video_mp2t,              "video/mp2t"},
    {ContentType::gz,                      "application/gzip"},
    {ContentType::doc,                     "application/msword"},
    {ContentType::docx,                    "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
    {ContentType::epub,                    "application/epub+zip"},
    {ContentType::json,                    "application/json"},
    {ContentType::json_ld,                 "application/ld+json"},
    {ContentType::pdf,                     "application/pdf"},
    {ContentType::php,                     "application/x-httpd-php"},
    {ContentType::xhtml,                   "application/xhtml+xml"},
    {ContentType::xml,                     "application/xml"},
    {ContentType::zip,                     "application/zip"},
    {ContentType::zip_7z,                  "application/x-7z-compressed"},
    {ContentType::jar,                     "application/java-archive"},
    {ContentType::atom,                    "application/atom+xml"},
    {ContentType::rss,                     "application/rss+xml"},
    {ContentType::custom,                  "application/octet-stream"}
});

constexpr StaticLookupTable<ContentType, std::string_view, 36> ext2ctx({
        {ContentType::zip_7z, "7z"},
        {ContentType::audio_aac, "aac"},
        {ContentType::video_avi, "avi"},
        {ContentType::image_bmp, "bmp"},
        {ContentType::text_css, "css"},
        {ContentType::text_csv, "csv"},
        {ContentType::doc, "doc"},
        {ContentType::docx, "docx"},
        {ContentType::epub, "epub"},
        {ContentType::image_gif,"gif"},
        {ContentType::gz,"gz"},
        {ContentType::text_html,"html"},
        {ContentType::image_ico, "ico"},
        {ContentType::jar,"jar"},
        {ContentType::image_jpeg,"jpeg"},
        {ContentType::text_javascript,"js"},
        {ContentType::json,"json"},
        {ContentType::audio_midi,"mid"},
        {ContentType::audio_midi, "midi"},
        {ContentType::video_mpeg, "mpeg"},
        {ContentType::audio_mpeg, "mp3"},
        {ContentType::audio_mp4, "mp4"},
        {ContentType::audio_ogg, "ogg"},
        {ContentType::pdf, "pdf"},
        {ContentType::php, "php"},
        {ContentType::image_png, "png"},
        {ContentType::image_svg, "svg"},
        {ContentType::image_tiff, "tiff"},
        {ContentType::font_ttf, "ttf"},
        {ContentType::text_plain, "txt"},
        {ContentType::audio_wav, "txt"},
        {ContentType::font_woff, "woff"},
        {ContentType::font_woff2, "woff2"},
        {ContentType::xhtml, "woff2"},
        {ContentType::xml, "xml"},
        {ContentType::zip, "zip"},
});

template<typename T, std::size_t n>
T enumFromString(const std::string_view (&list)[n], const std::string_view &txt) {
    strIEqual eq;
    int m = 0;
    for (const auto &x: list) {
        if (eq(x,txt)) return static_cast<T>(m);
        ++m;
    }
    return static_cast<T>(m-1);

}


ContentType extensionToContentType(const std::string_view &txt) {
    return ext2ctx[txt];
}



bool HeaderMap::headers(std::string_view hdrstr, HeaderMap &hdrmap, std::string_view &firstLine) {
    hdrmap.clear();
    auto lnsplt = splitAt(hdrstr, "\r\n");
    firstLine = lnsplt();
    if (firstLine.empty()) {
        return false;
    }
    auto ln = lnsplt();
    while (!ln.empty()) {
        // An HTTP header consists of its case-insensitive name followed by a colon (:),
        auto ddsplt = splitAt(ln,":");
        std::string_view key = ddsplt();
        std::string_view value = trim(ddsplt);
        if (key.empty()) return false;
        hdrmap.push_back({key,value});
        ln = lnsplt();
    }

    std::stable_sort(hdrmap.begin(), hdrmap.end(), [](const auto &a, const auto &b) {
        return strILess()(a.first, b.first);
    });


    return true;

}

HeaderValue HeaderMap::operator [](const std::string_view &name) const {
    auto iter = find(name);
    if (iter != end()) return iter->second;
    else return {};
}

HeaderMap::const_iterator HeaderMap::find(const std::string_view &name) const {
    auto iter = lower_bound(name);
    if (iter != end() && strIEqual()(iter->first, name)) return iter;
    else return end();
}

HeaderMap::const_iterator HeaderMap::lower_bound(
        const std::string_view &name) const {
    return std::lower_bound(begin(),end(),value_type(name,{}),cmpfn_lw);
}

HeaderMap::const_iterator HeaderMap::upper_bound(
        const std::string_view &name) const {
    return std::upper_bound(begin(),end(),value_type(name,{}),cmpfn_up);
}

std::pair<HeaderMap::const_iterator, HeaderMap::const_iterator> HeaderMap::equal_range(
        const std::string_view &name) const {
    return {
        lower_bound(name),upper_bound(name)
    };
}

bool HeaderMap::cmpfn_lw(const value_type &a, const value_type &b) {
    if (strILess()(a.first, b.first)) return true;
    if (strIEqual()(a.first, b.first)) {
        return a.second.length() < b.second.length();
    } else {
        return false;
    }
}

bool HeaderMap::cmpfn_up(const value_type &a, const value_type &b) {
    return strILess()(a.first, b.first);
}

std::string_view StatusCodeMap::message(int code) const {
    auto iter = std::lower_bound(begin(), end(), std::pair(code, std::string_view()),
            [&](const auto &a, const auto &b){return a.first <b.first;});
    if (iter == end() || iter->first != code) return std::string_view();
    else return iter->second;
}

}

}
