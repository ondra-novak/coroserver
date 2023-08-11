#pragma once
#ifndef SRC_COROSERVER_HTTP_STRINGTABLES_H_
#define SRC_COROSERVER_HTTP_STRINGTABLES_H_

#include "http_common.h"

namespace coroserver {

namespace http {


constexpr auto strMethod = makeStaticLookupTable<Method, std::string_view>({
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

constexpr auto strStatusMessages = makeStaticLookupTable<int, std::string_view>({
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


constexpr auto strVer = makeStaticLookupTable<Version, std::string_view> ({
    {Version::http1_0, "HTTP/1.0"},
    {Version::http1_1, "HTTP/1.1"},
    {Version::unknown, "unknown"}
});

constexpr auto strContentType = makeStaticLookupTable<ContentType, std::string_view>  ({
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

constexpr auto ext2ctx = makeStaticLookupTable<ContentType, std::string_view> ({
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
        {ContentType::image_jpeg,"jpg"},
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
        {ContentType::audio_wav, "wav"},
        {ContentType::font_woff, "woff"},
        {ContentType::font_woff2, "woff2"},
        {ContentType::xhtml, "xhtml"},
        {ContentType::xml, "xml"},
        {ContentType::zip, "zip"},
});

namespace strtable {

constexpr std::string_view hdr_allow("Allow");
constexpr std::string_view hdr_accept("Accept");
constexpr std::string_view hdr_access_control_allow_origin("Access-Control-Allow-Origin");
constexpr std::string_view hdr_access_control_allow_credentials("Access-Control-Allow-Credentials");
constexpr std::string_view hdr_access_control_allow_headers("Access-Control-Allow-Headers");
constexpr std::string_view hdr_access_control_allow_methods("Access-Control-Allow-Methods");
constexpr std::string_view hdr_access_control_request_method("Access-Control-Request-Method");
constexpr std::string_view hdr_access_control_request_headers("Access-Control-Request-Headers");
constexpr std::string_view hdr_authorization("Authorization");
constexpr std::string_view val_basic("basic");
constexpr std::string_view val_bearer("bearer");
constexpr std::string_view hdr_cache_control("Cache-Control");
constexpr std::string_view hdr_host("Host");
constexpr std::string_view hdr_content_type("Content-Type");
constexpr std::string_view hdr_content_length("Content-Length");
constexpr std::string_view hdr_transfer_encoding("Transfer-Encoding");
constexpr std::string_view val_chunked("chunked");
constexpr std::string_view hdr_connection("Connection");
constexpr std::string_view val_close("close");
constexpr std::string_view val_keep_alive("keep-alive");
constexpr std::string_view val_upgrade("upgrade");
constexpr std::string_view hdr_set_cookie("Set-Cookie");
constexpr std::string_view hdr_sep("\r\n");
constexpr std::string_view hdr_date("Date");
constexpr std::string_view hdr_server("Server");
constexpr std::string_view hdr_cookie("Cookie");
constexpr std::string_view hdr_expect("Expect");
constexpr std::string_view val_100_continue("100-continue");
constexpr std::string_view hdr_etag("ETag");
constexpr std::string_view hdr_if_none_match("If-None-Match");
constexpr std::string_view hdr_last_modified("Last-Modified");
constexpr std::string_view hdr_origin("Origin");
constexpr std::string_view hdr_pragma("Pragma");
constexpr std::string_view hdr_user_agent("User-Agent");
constexpr std::string_view hdr_x_forwarded_for("X-Forwarded-For");
constexpr std::string_view hdr_forwarded("Forwarded");
constexpr std::string_view hdr_x_forwarded_host("X-Forwarded-Host");
constexpr std::string_view hdr_x_forwarded_proto("X-Forwarded-Proto");
constexpr std::string_view hdr_x_forwarded_prefix("X-Forwarded-Prefix");
constexpr std::string_view hdr_front_end_https("Front-End-Https");
constexpr std::string_view hdr_location("Location");
constexpr std::string_view hdr_upgrade("Upgrade");
constexpr std::string_view val_websocket("websocket");
constexpr std::string_view hdr_www_authenticate("WWW-Authenticate");
constexpr std::string_view hdr_refresh("Refresh");
constexpr std::string_view hdr_x_accel_buffering("X-Accel-Buffering");



}


}
}




#endif /* SRC_COROSERVER_HTTP_STRINGTABLES_H_ */
