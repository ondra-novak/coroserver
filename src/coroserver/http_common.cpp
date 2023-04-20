#include "http_common.h"
#include "strutils.h"

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
std::string_view hdr_x_forwarded_host("X-Forwarded-Host");
std::string_view hdr_x_forwarded_proto("X-Forwarded-Proto");
std::string_view hdr_front_end_https("Front-End-Https");
std::string_view hdr_location("Location");
std::string_view hdr_upgrade("Upgrade");
std::string_view val_websocket("websocket");
std::string_view hdr_www_authenticate("WWW-Authenticate");
std::string_view hdr_refresh("Refresh");
std::string_view hdr_x_accel_buffering("X-Accel-Buffering");


}

StatusCodeMap StatusCodeMap::instance({
        {100,"Continue"},
        {101,"Switching Protocols"},
        {102,"Processing"},
        {103,"Early Hints"},
        {200,"OK"},
        {201,"Created"},
        {202,"Accepted"},
        {203,"Non-Authoritative Information"},
        {204,"No Content"},
        {205,"Reset Content"},
        {206,"Partial Content"},
        {207,"Multi-Status"},
        {208,"Already Reported"},
        {300,"Multiple Choices"},
        {301,"Moved Permanently"},
        {302,"Found"},
        {303,"See Other"},
        {304,"Not Modified"},
        {305,"Use Proxy"},
        {307,"Temporary Redirect"},
        {308,"Permanent Redirect"},
        {400,"Bad Request"},
        {401,"Unauthorized"},
        {402,"Payment Required"},
        {403,"Forbidden"},
        {404,"Not Found"},
        {405,"Method Not Allowed"},
        {406,"Not Acceptable"},
        {407,"Proxy Authentication Required"},
        {408,"Request Timeout"},
        {409,"Conflict"},
        {410,"Gone"},
        {411,"Length Required"},
        {412,"Precondition Failed"},
        {413,"Request Entity Too Large"},
        {414,"Request-URI Too Long"},
        {415,"Unsupported Media Type"},
        {416,"Requested Range Not Satisfiable"},
        {417,"Expectation Failed"},
        {418,"I'm a teapot"},
        {419,"Page Expired"},
        {420,"Method Failure"},
        {421,"Misdirected Request"},
        {422,"Unprocessable Entity"},
        {423,"Locked"},
        {424,"Failed Dependency"},
        {425,"Too Early"},
        {426,"Upgrade Required"},
        {428,"Precondition Required"},
        {429,"Too Many Requests"},
        {431,"Request Header Fields Too Large"},
        {451,"Unavailable For Legal Reasons"},
        {500,"Internal Server Error"},
        {501,"Not Implemented"},
        {502,"Bad Gateway"},
        {503,"Service Unavailable"},
        {504,"Gateway Timeout"},
        {505,"HTTP Version Not Supported"},
        {506,"Variant Also Negotiates"},
        {507,"Insufficient Storage"},
        {508,"Loop Detected"},
        {510,"Not Extended"},
        {511,"Network Authentication Required"}
});

static std::string_view methods[]={
    "",
    "GET",
    "HEAD",
    "POST",
    "PUT",
    "DELETE",
    "CONNECT",
    "OPTIONS",
    "TRACE",
    "PATCH",
};
static std::string_view versions[]={
    "",
    "HTTP/1.0",
    "HTTP/1.1",
    "unknown"
};

static std::string_view httpProxyType[] = {
        "no_proxy",
        "nginx",
        "apache",
        "generic"
};

static std::string_view contenTypeList[] = {
        "application/octet-stream",
        "audio/aac",
        "audio/midi",
        "audio/mpeg",
        "audio/mp4",
        "audio/ogg",
        "audio/opus",
        "audio/wav",
        "text/plain",
        "text/plain;charset=utf-8",
        "text/html",
        "text/html;charset=utf-8",
        "text/css",
        "text/csv",
        "text/javascript",
        "text/event-stream",
        "image/avif",
        "image/bmp",
        "image/gif",
        "image/vnd.microsoft.icon",
        "image/png",
        "image/svg+xml",
        "image/tiff",
        "image/jpeg",
        "font/otf"
        "font/ttf",
        "font/woff",
        "font/woff2",
        "video/x-msvideo",
        "video/mpeg",
        "video/ogg",
        "video/mp2t",
        "application/gzip",
        "application/msword",
        "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
        "application/epub+zip",
        "application/json",
        "application/ld+json",
        "application/pdf",
        "application/x-httpd-php",
        "application/xhtml+xml",
        "application/xml",
        "application/zip",
        "application/x-7z-compressed",
        "application/java-archive",
        "application/atom+xml",
        "application/rss+xml",
        "application/octet-stream"

};

static std::pair<std::string_view, ContentType> ext2ctx[] = {
        {"7z", ContentType::zip_7z},
        {"aac", ContentType::audio_aac},
        {"avi", ContentType::video_avi},
        {"bmp", ContentType::image_bmp},
        {"css", ContentType::text_css},
        {"csv", ContentType::text_csv},
        {"doc", ContentType::doc},
        {"docx", ContentType::docx},
        {"epub", ContentType::epub},
        {"gif", ContentType::image_gif},
        {"gz", ContentType::gz},
        {"html", ContentType::text_html},
        {"ico", ContentType::image_ico},
        {"jar", ContentType::jar},
        {"jpeg", ContentType::image_jpeg},
        {"js", ContentType::text_javascript},
        {"json", ContentType::json},
        {"mid", ContentType::audio_midi},
        {"midi", ContentType::audio_midi},
        {"mpeg", ContentType::video_mpeg},
        {"mp3", ContentType::audio_mpeg},
        {"mp4", ContentType::audio_mp4},
        {"ogg", ContentType::audio_ogg},
        {"pdf", ContentType::pdf},
        {"php", ContentType::php},
        {"png", ContentType::image_png},
        {"svg", ContentType::image_svg},
        {"tiff", ContentType::image_tiff},
        {"ttf", ContentType::font_ttf},
        {"txt", ContentType::text_plain},
        {"wav", ContentType::audio_wav},
        {"woff", ContentType::font_woff},
        {"woff2", ContentType::font_woff2},
        {"xhtml", ContentType::xhtml},
        {"xml", ContentType::xml},
        {"zip", ContentType::zip},
};

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

Method strMethod(const std::string_view &txt) {
    if (txt.empty()) return Method::not_set;
    return enumFromString<Method>(methods, txt);

}
std::string_view strMethod(Method m) {
    if (m == Method::unknown) return "<unknown>";
    return methods[static_cast<int>(m)];
}

Version strVer(const std::string_view &txt) {
    if (txt.empty()) return Version::not_set;
    return enumFromString<Version>(versions, txt);

}
std::string_view strVer(Version m) {
    if (m == Version::unknown) return "<unknown>";
    return versions[static_cast<int>(m)];
}
ProxyType strProxyType(const std::string_view &txt)  {
    if (txt.empty()) return ProxyType::no_proxy;
    return enumFromString<ProxyType>(httpProxyType, txt);

}
std::string_view strProxyType(ProxyType m) {
    if (m == ProxyType::unknown) return "<unknown>";
    return httpProxyType[static_cast<int>(m)];
}

ContentType strContentType(const std::string_view &txt) {
    if (txt.empty()) return ContentType::binary;
    return enumFromString<ContentType>(contenTypeList, txt);
}
std::string_view strContentType(ContentType m) {
    return contenTypeList[static_cast<int>(m)];
}

ContentType extensionToContentType(const std::string_view &txt) {
    strILess cmp;
    strIEqual eq;
    auto iter = std::lower_bound(std::begin(ext2ctx), std::end(ext2ctx), std::pair(txt, ContentType::binary),
            [&](const auto &a, const auto &b) {
        return cmp(a.first, b.first);
    });
    if (iter == std::end(ext2ctx) || !eq(iter->first,txt)) return ContentType::binary;
    return iter->second;
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
