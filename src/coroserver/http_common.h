#pragma once
#ifndef SRC_USERVER_HTTP_COMMON_H_
#define SRC_USERVER_HTTP_COMMON_H_
#include "static_lookup.h"
#include "strutils.h"
#include <cctype>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <vector>


namespace coroserver {

namespace http {

enum class Method {
        not_set = 0,
        GET,
        HEAD,
        POST,
        PUT,
        DELETE,
        CONNECT,
        OPTIONS,
        TRACE,
        PATCH,
        unknown
};

enum class Version {
    not_set = 0,
    http1_0,
    http1_1,
    unknown
};

enum class ContentType {
    binary = 0,
    audio_aac,
    audio_midi,
    audio_mpeg,
    audio_mp4,
    audio_ogg,
    audio_opus,
    audio_wav,
    text_plain,
    text_plain_utf8,
    text_html,
    text_html_utf8,
    text_css,
    text_csv,
    text_javascript,
    event_stream,
    image_avif,
    image_bmp,
    image_gif,
    image_ico,
    image_png,
    image_svg,
    image_tiff,
    image_jpeg,
    font_otf,
    font_ttf,
    font_woff,
    font_woff2,
    video_avi,
    video_mpeg,
    video_ogg,
    video_mp2t,
    gz,
    doc,
    docx,
    epub,
    json,
    json_ld,
    pdf,
    php,
    xhtml,
    xml,
    zip,
    zip_7z,
    jar,
    atom,
    rss,
    custom
};

extern StaticLookupTable<Method, std::string_view, 10> strMethod;
extern StaticLookupTable<Version, std::string_view, 3> strVer;
extern StaticLookupTable<ContentType, std::string_view, 48> strContentType;
extern StaticLookupTable<int, std::string_view, 63> strStatusMessages;

ContentType extensionToContentType(const std::string_view &txt);


struct strILess {
    bool operator()(const std::string_view &a, const std::string_view &b) const {
        auto ln = std::min(a.length(), b.length());
        for (std::size_t i = 0; i < ln; i++) {
            int c = std::tolower(a[i]) - std::tolower(b[i]);
            if (c) return c<0;
        }
        return (static_cast<int>(a.length()) - static_cast<int>(b.length())) < 0;
    }
};
struct strIEqual {
    bool operator()(const std::string_view &a, const std::string_view &b) const {
        if (a.length() != b.length()) return false;
        auto ln = a.length();
        for (std::size_t i = 0; i < ln; i++) {
            int c = std::tolower(a[i]) - std::tolower(b[i]);
            if (c) return false;
        }
        return true;
    }
};

///Header value, values are in most cases used in headers
/**
 * However, it can be also used in queries. Default comparison for value is case-insensitive comparison
 *
 * The header value might not be defined, You can check whether value is defined or not
 */
class HeaderValue {
public:
    HeaderValue():_defined(false) {}
    template<typename X>
    HeaderValue(X &&text):_text(std::forward<X>(text)),_defined(true) {}

    bool operator==(const HeaderValue &other) const {
        if (_defined) return other._defined && strIEqual()(_text, other._text);
        else return !other._defined;
    }
    bool operator!=(const HeaderValue &other) const {
        return !operator==(other);
    }
    bool operator==(const std::string_view &other) const {
        if (_defined) return strIEqual()(_text, other);
        return false;
    }
    bool operator!=(const std::string_view &other) const {
        return !operator==(other);
    }

    std::size_t get_uint() const {
        std::size_t v = 0;
        if (std::from_chars(_text.data(), _text.data()+_text.length(), v, 10).ec != std::errc()) return 0;
        else return v;
    }

    std::ptrdiff_t get_int() const {
        std::ptrdiff_t v = 0;
        if (std::from_chars(_text.data(), _text.data()+_text.length(), v, 10).ec != std::errc()) return 0;
        else return v;
    }

    bool has_value() const {return _defined;}

    operator std::string_view() const {return _text;}
    operator std::string() const {return std::string(_text);}

    operator bool() const {return _defined;}
    bool operator!() const {return !_defined;}
    std::string_view view() const { return _text;}
protected:
    std::string_view _text;
    bool _defined;
};


class HeaderMap : public std::vector<std::pair<std::string_view, std::string_view> > {
public:
    using value_type = std::pair<std::string_view, std::string_view> ;
    static bool headers(std::string_view hdrtext, HeaderMap &hdrmap, std::string_view &firstLine);

    HeaderValue operator[](const std::string_view &name) const;
    const_iterator find(const std::string_view &name) const;
    const_iterator lower_bound(const std::string_view &name) const;
    const_iterator upper_bound(const std::string_view &name) const;
    std::pair<const_iterator, const_iterator> equal_range(const std::string_view &name) const;

    static bool cmpfn_lw(const value_type &a, const value_type &b);
    static bool cmpfn_up(const value_type &a, const value_type &b);

};

class StatusCodeMap: public std::vector<std::pair<int, std::string_view> > {
public:
    using std::vector<std::pair<int, std::string_view> >::vector;

    std::string_view message(int code) const;

    static StatusCodeMap instance;
};

struct ForwardedHeader{
    ///specifies ID proxy who performs forwarding
    std::string_view by;
    ///specifies IP address of client connected to proxy
    std::string_view for_client;
    ///specifies original host
    std::string_view host;
    ///specifies original protocol
    std::string_view proto;

    ForwardedHeader(std::string_view text) {

        constexpr StaticLookupTable<int, std::string_view,4> items({
            {1, "by"},
            {2, "for"},
            {3, "host"},
            {4, "proto"}
        });

        auto splt = splitSeparated(text, ",;");
        std::string_view token = splt();
        while (!token.empty()) {
            auto kpos = token.find('=');
            if (kpos != token.npos) {
                std::string_view key = trim(token.substr(0,kpos));
                std::string_view value = trim(token.substr(kpos+1));
                if (!value.empty() && value.front() == '"' && value.back() == '"') {
                    value = value.substr(1, value.length()-2);
                }
                switch (items[key]) {
                    case 1: if (by.empty()) by = value;break;
                    case 2: if (for_client.empty()) for_client= value;break;
                    case 3: if (host.empty()) host = value;break;
                    case 4: if (proto.empty()) proto = value;break;
                    default:break;
                }
            }
            token = splt();
        }
    }

};


namespace strtable {

extern std::string_view hdr_allow;
extern std::string_view hdr_accept;
extern std::string_view hdr_access_control_request_method;
extern std::string_view hdr_access_control_request_headers;
extern std::string_view hdr_authorization;
extern std::string_view val_basic;
extern std::string_view val_bearer;
extern std::string_view hdr_cache_control;
extern std::string_view hdr_host;
extern std::string_view hdr_content_type;
extern std::string_view hdr_content_length;
extern std::string_view hdr_transfer_encoding;
extern std::string_view val_chunked;
extern std::string_view hdr_connection;
extern std::string_view val_close;
extern std::string_view val_upgrade;
extern std::string_view hdr_set_cookie;
extern std::string_view hdr_sep;
extern std::string_view hdr_date;
extern std::string_view hdr_server;
extern std::string_view hdr_cookie;
extern std::string_view hdr_expect;
extern std::string_view val_100_continue;
extern std::string_view hdr_etag;
extern std::string_view hdr_if_none_match;
extern std::string_view hdr_origin;
extern std::string_view hdr_pragma;
extern std::string_view hdr_user_agent;
extern std::string_view hdr_x_forwarded_for;
extern std::string_view hdr_forwarded;
extern std::string_view hdr_x_forwarded_host;
extern std::string_view hdr_x_forwarded_proto;
extern std::string_view hdr_front_end_https;
extern std::string_view hdr_location;
extern std::string_view hdr_upgrade;
extern std::string_view val_websocket;
extern std::string_view hdr_www_authenticate;
extern std::string_view hdr_refresh;
extern std::string_view hdr_x_accel_buffering;
extern std::string_view val_keep_alive;
extern std::string_view hdr_last_modified;


}

}


}




#endif /* SRC_USERVER_HTTP_COMMON_H_ */

