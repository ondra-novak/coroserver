#pragma once

#include <string_view>
#include <string>
#include <filesystem>
#include "utils/named_enum_class.hpp"

namespace coroserver {

namespace http {

constexpr std::string_view split_at(std::string_view &line, std::string_view sep) {
    std::string_view out;
    auto pos = line.find(sep);
    if (pos == line.npos) {
        out = line;
        line = {};
    } else {
        out = line.substr(0, pos);
        line = line.substr(pos+sep.size());
    }
    return out;
}

constexpr bool fast_is_space(char c) {
    return c>=0 && c <= 32;
}

constexpr std::string_view trim(std::string_view text) {
    while (!text.empty() && fast_is_space(text.front())) text = text.substr(1);
    while (!text.empty() && fast_is_space(text.back())) text = text.substr(0,text.length()-1);
    return text;
}

 std::string normalize_uri(std::string_view uri);
std::optional<std::filesystem::path> map_uri_to_path(std::filesystem::path base_path, std::string_view uri);


class HeaderKey : public std::string_view {
public:
    constexpr HeaderKey() = default;
    constexpr HeaderKey(const std::string_view &x):std::string_view(x) {}
    using std::string_view::string_view;

    constexpr int compare(const HeaderKey &other) const noexcept  {
        std::size_t csz = std::min(size(), other.size());
        for (std::size_t i = 0; i < csz; ++i) {
            char c1 = fast_to_upper((*this)[i]);
            char c2 = fast_to_upper(other[i]);
            int diff = static_cast<int>(static_cast<unsigned char>(c1))
                        - static_cast<int>(static_cast<unsigned char>(c2));
            if (diff) return diff;
        }
        return size() > other.size()?1:size()<other.size()?-1:0;
    }

    constexpr bool operator==(const HeaderKey &other) const noexcept {
        return compare(other) == 0;
    }

    constexpr auto operator<=>(const HeaderKey &other) const noexcept {
        int result = compare(other);
        if (result < 0) return std::strong_ordering::less;
        if (result > 0) return std::strong_ordering::greater;
        return std::strong_ordering::equal;
    }

    constexpr static char fast_to_upper(char c) {
        return c>='a' && c <='z'?c-'a'+'A':c;
    }
};

using HeaderValue = std::optional<std::string_view>;

enum class Method : std::uint8_t{
        unknown,
        GET,
        HEAD,
        POST,
        PUT,
        DELETE,
        CONNECT,
        OPTIONS,
        TRACE,
        PATCH
};

enum class Protocol :std::uint8_t {
        unknown,
        HTTP_1_0,
        HTTP_1_1
};


constexpr auto methods = makeStaticLookupTable<Method, HeaderKey>({
    {Method::unknown, "unknown"},
    {Method::GET, "GET"},
    {Method::HEAD, "HEAD"},
    {Method::POST, "POST"},
    {Method::PUT, "PUT"},
    {Method::DELETE, "DELETE"},
    {Method::CONNECT, "CONNECT"},
    {Method::OPTIONS, "OPTIONS"},
    {Method::TRACE, "TRACE"},
    {Method::PATCH, "PATCH"}
});
constexpr auto protocols = makeStaticLookupTable<Protocol, HeaderKey>({
        {Protocol::unknown, "unknown"},
        {Protocol::HTTP_1_0, "HTTP/1.0"},
        {Protocol::HTTP_1_1, "HTTP/1.1"},
});


constexpr auto response_status_codes = makeStaticLookupTable<unsigned int, std::string_view>({
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
    {226,"IM Used"},
    {300,"Multiple Choices"},
    {301,"Moved Permanently"},
    {302,"Found"},
    {303,"See Other"},
    {304,"Not Modified"},
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
    {413,"Content Too Large"},
    {414,"URI Too Long"},
    {415,"Unsupported Media Type"},
    {416,"Range Not Satisfiable"},
    {417,"Expectation Failed"},
    {418,"I'm a teapot"},
    {421,"Misdirected Request"},
    {422,"Unprocessable Content"},
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
    {511,"Network Authentication Required"},
});

enum class ContentType : std::uint8_t{
    plain,
    html,
    css,
    javascript,
    json,
    xml,
    csv,
    markdown,
    jpeg,
    png,
    gif,
    webp,
    svg,
    avif,
    bmp,
    ico,
    mp3,
    ogg_audio,
    wav,
    aac,
    flac,
    mp4,
    webm_video,
    ogg_video,
    pdf,
    doc,
    docx,
    xls,
    xlsx,
    ppt,
    pptx,
    zip,
    tar,
    gzip,
    rar,
    form_urlencoded,
    multipart_form_data,
    octet_stream
};

constexpr auto content_types = makeStaticLookupTable<ContentType, std::string_view>({

            {ContentType::plain, "text/plain"},
            {ContentType::html, "text/html"},
            {ContentType::css, "text/css"},
            {ContentType::javascript, "application/javascript"},
            {ContentType::json, "application/json"},
            {ContentType::xml, "application/xml"},
            {ContentType::csv, "text/csv"},
            {ContentType::markdown, "text/markdown"},
            {ContentType::jpeg, "image/jpeg"},
            {ContentType::png, "image/png"},
            {ContentType::gif, "image/gif"},
            {ContentType::webp, "image/webp"},
            {ContentType::svg, "image/svg+xml"},
            {ContentType::avif, "image/avif"},
            {ContentType::bmp, "image/bmp"},
            {ContentType::ico, "image/x-icon"},
            {ContentType::mp3, "audio/mpeg"},
            {ContentType::ogg_audio, "audio/ogg"},
            {ContentType::wav, "audio/wav"},
            {ContentType::aac, "audio/aac"},
            {ContentType::flac, "audio/flac"},
            {ContentType::mp4, "video/mp4"},
            {ContentType::webm_video, "video/webm"},
            {ContentType::ogg_video, "video/ogg"},
            {ContentType::pdf, "application/pdf"},
            {ContentType::doc, "application/msword"},
            {ContentType::docx, "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
            {ContentType::xls, "application/vnd.ms-excel"},
            {ContentType::xlsx, "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
            {ContentType::ppt, "application/vnd.ms-powerpoint"},
            {ContentType::pptx, "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
            {ContentType::zip, "application/zip"},
            {ContentType::tar, "application/x-tar"},
            {ContentType::gzip, "application/gzip"},
            {ContentType::rar, "application/x-rar-compressed"},
            {ContentType::form_urlencoded, "application/x-www-form-urlencoded"},
            {ContentType::multipart_form_data, "multipart/form-data"},
            {ContentType::octet_stream, "application/octet-stream"}
});

constexpr auto content_types_to_extension = makeStaticLookupTable<ContentType, std::string_view>({

    {ContentType::plain,"txt"},
            {ContentType::html,"html"},
            {ContentType::html,"htm"},
            {ContentType::css,"css"},
            {ContentType::javascript,"js"},
            {ContentType::json,"json"},
            {ContentType::xml,"xml"},
            {ContentType::csv,"csv"},
            {ContentType::markdown,"md"},

            {ContentType::jpeg,"jpg"},
            {ContentType::jpeg,"jpeg"},
            {ContentType::png,"png"},
            {ContentType::gif,"gif"},
            {ContentType::webp,"webp"},
            {ContentType::svg,"svg"},
            {ContentType::avif,"avif"},
            {ContentType::bmp,"bmp"},
            {ContentType::ico,"ico"},

            {ContentType::mp3,"mp3"},
            {ContentType::ogg_audio,"ogg"},
            {ContentType::wav,"wav"},
            {ContentType::aac,"aac"},
            {ContentType::flac,"flac"},

            {ContentType::mp4,"mp4"},
            {ContentType::webm_video,"webm"},
            {ContentType::ogg_video,"ogv"},

            {ContentType::pdf,"pdf"},
            {ContentType::doc,"doc"},
            {ContentType::docx,"docx"},
            {ContentType::xls,"xls"},
            {ContentType::xlsx,"xlsx"},
            {ContentType::ppt,"ppt"},
            {ContentType::pptx,"pptx"},

            {ContentType::zip,"zip"},
            {ContentType::tar,"tar"},
            {ContentType::gzip,"gz"},
            {ContentType::rar,"rar"},

            {ContentType::octet_stream,"bin"},
            {ContentType::octet_stream,"exe"},
            {ContentType::octet_stream,"dat"},
            {ContentType::form_urlencoded,"form"}
});

enum class RedirectType {
    /// Permanent redirect using GET method (status 301)
    permanent_get = 301,
    /// Permanent redirect, preserving the original request method and body (status 308)
    permanent = 308,
    /// Temporary redirect using GET method (status 303)
    temporary_get = 303,
    /// Temporary redirect, preserving the original request method and body (status 307)
    temporary = 307
};

enum class ConnectionType : std::uint8_t {
    ///direct connection with the client, no proxy in path, unsecured
    /**
     * The web server expects no mapping, no proxying, and no security on path
     *
     * - the protocol defaults to http://
     *
     * - no mapping, so path = uri
     *
     * - http://host/path
     *
     */
    direct_unsecure,
    ///direct connection with the client, secure connection
    /**
     * The web server expects no mapping, no proxying, but https
     *
     * - the protocol default to https://
     *
     * - no mapping
     *
     * - https://host/path
     */
    direct_secure,

    ///Connection over reverse proxy
    /**
     * The web server expects connection over proxy, it searches for
     * headers to find out location of the server
     *
     * - it expects Host, X-Forwarded-Proto, X-Forwarded-Prefix
     * - rewrite rules are not recommended
     * - proto://host/prefix/path
     *
     * In this mode, the direct connection is still posible. But in this
     * mode, the server is on risk because it will try to find
     * and process above headers.
     *
     */
    reverse_proxy
};

}

}
