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
#include <variant>
#include <concepts>


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


ContentType extensionToContentType(const std::string_view &txt);

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

///Split path to parts separated by /
/**
 * @param vpath path or virtual path. Query part is automatically removed
 * @return function which returns next part for each call. It skips any empty parts.
 * Automatically performs URL-decode on each part. Function returns empty string if there
 * are no more parts
 */
inline auto split_path(std::string_view vpath) {
    vpath = vpath.substr(0,std::min(vpath.find('?'), vpath.length()));
    return [vpath, buffer = std::string(), splt = splitAt<char>(vpath, "/")]() mutable {
        while (splt) {
            auto part = splt();
            if (!part.empty()) {
                buffer.clear();
                url::decode(part, [&](char c){buffer.push_back(c);});
                return std::string_view(buffer);
            }
        }
        return std::string_view();
    };
}

namespace _details {

    template<typename ... Args> class TypeList;

    template<typename T, typename X> struct MakeQueryValuesVariant;

    template<typename T, typename ... Args> struct MakeQueryValuesVariant<T, TypeList<Args...>> {
    using Type = std::variant<std::monostate, Args T::* ..., std::optional<Args> T::* ...>;

    };

    template<typename X>
    struct is_optional {
        static constexpr bool value = false;
        using Type = X;
    };

    template<typename X>
    struct is_optional<std::optional<X> > {
        static constexpr bool value = true;
        using Type = X;
    };


}

///Contains list of supported types fields that can appear in query
using SupportedQueryValueTypes = _details::TypeList<std::uint16_t, std::int16_t, std::uint32_t, std::int32_t, std::uint64_t, std::int64_t, float, double, char, std::string, bool>;

///Contains std::variant of all possible references to fields of given type T
template<typename T>
using SupportedQueryValueVariant = typename _details::MakeQueryValuesVariant<T, SupportedQueryValueTypes>::Type;


///Contains reference to a field in object T
/**
 * @tparam T structure or object where values from the query will be stored
 *
 * The supported types are specified in SupportedQueryValueTypes. The reference to a
 * field is specified as pointer to a member variable (T::*). The variable can
 * have one of supported type or std::optional of supported type. The std::optional
 * template allows to detect, whether the query contained the field associated
 * with the variable in question
 */
template<typename T>
class QueryValueRef: public SupportedQueryValueVariant<T> {
public:
    using SupportedQueryValueVariant<T>::SupportedQueryValueVariant;
    bool operator<(const QueryValueRef &) = delete;
    bool operator>(const QueryValueRef &) = delete;
    bool operator<=(const QueryValueRef &) = delete;
    bool operator>=(const QueryValueRef &) = delete;
};

///Declaration of type, which contains mapping table from string keys to variable names
/**
 *
 * To construct this
 * @code
 * {"foo", &T::foo},   //"foo" -> &T::foo
 * {"bar", &T::bar},   //"bar" -> &T::bar
 * @endcode
 *
 * To construct variable if this type, use makeQueryFieldMap
 */
template<typename T, int N>
using QueryFieldMap = StaticLookupTable<std::string_view, QueryValueRef<T>, N>;


///Parse form urlencoded content and enumerate key/value pairs
/**
 * @param content content
 * @param fn function which receives key/value pair
 */
template<std::invocable<std::string, std::string> Fn>
void parse_form_urlencoded_enum_kv(std::string_view content, Fn &&fn) {
    std::string key;
    std::string value;
    auto spltFields = splitAt(content, "&");
    while (spltFields) {
        std::string_view item = spltFields();
        if (item.empty()) continue;
        auto eq = item.find('=');
        if (eq == item.npos) {
            key.clear();
            url::decode(item,[&](char c){key.push_back(c);});
            value = "1";
        } else {
            key.clear();
            value.clear();
            url::decode(item.substr(0,eq),[&](char c){key.push_back(c);});
            url::decode(item.substr(eq+1),[&](char c){value.push_back(c);});
        }
        fn(key, value);
    }

}

///Parse content www-form-urlencoded
/**
 * @param content to parse
 *
 * @param map Mapping table from key to field. Use makeQueryFieldMap() to construct such
 * object
 * @param target target object, where the values will be stored
 * @return count of successfully stored values.
 */
template<typename T, int N>
std::size_t parse_form_urlencoded(std::string_view content, const QueryFieldMap<T,N> &map, T &target) {
    if (content.empty()) return 0;
    std::size_t fld_count = 0;
    parse_form_urlencoded_enum_kv(content, [&](const std::string &key, const std::string &value){
        QueryValueRef<T> fldref = map[key];
        if (std::holds_alternative<std::monostate>(fldref)) return;
        std::visit([&](auto ref){
            using ItemT = decltype(ref);
            if constexpr(std::is_member_object_pointer_v<ItemT>) {
                using PtrType = std::remove_reference_t<decltype(target.*ref)>;
                using Type = std::remove_reference_t<typename _details::is_optional<PtrType>::Type>;
                if constexpr(std::is_same_v<Type,std::uint16_t>) {
                    target.*ref =  static_cast<std::uint16_t>(std::strtoul(value.c_str(),nullptr,10));
                } else if constexpr(std::is_same_v<Type,std::int16_t>) {
                    target.*ref =  static_cast<std::int16_t>(std::strtol(value.c_str(),nullptr,10));
                } else if constexpr(std::is_same_v<Type,std::uint32_t>) {
                    target.*ref =  static_cast<std::uint32_t>(std::strtoul(value.c_str(),nullptr,10));
                } else if constexpr(std::is_same_v<Type,std::int32_t>) {
                    target.*ref =  static_cast<std::int32_t>(std::strtol(value.c_str(),nullptr,10));
                } else if constexpr(std::is_same_v<Type,std::uint64_t>) {
                    target.*ref =  static_cast<std::uint64_t>(std::strtoull(value.c_str(),nullptr,10));
                } else if constexpr(std::is_same_v<Type,std::int64_t>) {
                    target.*ref =  static_cast<std::int64_t>(std::strtoll(value.c_str(),nullptr,10));
                } else if constexpr(std::is_same_v<Type,float>) {
                    target.*ref =  static_cast<float>(std::strtod(value.c_str(),nullptr));
                } else if constexpr(std::is_same_v<Type,double>) {
                    target.*ref =  static_cast<double>(std::strtod(value.c_str(),nullptr));
                } else if constexpr(std::is_same_v<Type,char>) {
                    target.*ref =  value.c_str()[0];
                } else if constexpr(std::is_same_v<Type,std::string>) {
                    target.*ref =  value;
                } else  {
                    static_assert(std::is_same_v<Type,bool>);
                    if (value == "true" || value == "1" || value == "yes" || value == "on") {
                        target.*ref = true;
                    } else if (value == "false" || value == "0" || value == "no" || value == "off") {
                        target.*ref = false;
                    } else return;
                }
                ++fld_count;
            }
        }, fldref);
    });
    return fld_count;

}


///Parse the query and store values to a target object
/**
 * @param vpath path or virtual path which contains the query. The query must be behind
 * character '?'. Anything before this character is ignored
 *
 * @param map Mapping table from key to field. Use makeQueryFieldMap() to construct such
 * object
 * @param target target object, where the values will be stored
 * @return count of successfuly stored values.
 */
template<typename T, int N>
std::size_t parse_query(std::string_view vpath, const QueryFieldMap<T,N> &map, T &target) {
    if (vpath.empty()) return 0;
    vpath = vpath.substr(std::min(vpath.length()-1, vpath.find('?'))+1);
    return parse_form_urlencoded(vpath, map, target);
}

template<typename T, int N, typename Fn>
void build_query(const T &source, const QueryFieldMap<T, N> &map, Fn &&output) {
    std::string buff;

    auto do_output = [&](const auto &item) {
        using Type = std::decay_t<decltype(item)>;
        if constexpr(std::is_same_v<T, char>) {
            buff.clear();
            buff.push_back(item);
        } else if constexpr(std::is_same_v<Type , std::string>) {
            url::encode(item, output);
            return;
        } else if constexpr(std::is_arithmetic_v<Type >) {
            buff = std::to_string(item);
        } else {
            static_assert(std::is_same_v<Type,bool>);
            if (item) buff = "true"; else buff="false";
        }
        url::encode(buff, output);
    };

    bool print_sep = false;
    for (const auto &pair: map) {
        if (print_sep) output('&');
        print_sep = std::visit([&](auto ref){
            using ItemT = decltype(ref);
            if constexpr(std::is_member_object_pointer_v<ItemT>) {
                using PtrType = std::decay_t<decltype(source.*ref)>;
                constexpr bool is_optional = _details::is_optional<PtrType>::value;
                if constexpr(is_optional) {
                    const auto &v = source.*ref;
                    if (v.has_value()) {
                        url::encode(pair.key, output);
                        output('=');
                        do_output(*v);
                        return true;
                    }
                } else {
                    const auto &v = source.*ref;
                    url::encode(pair.key, output);
                    output('=');
                    do_output(v);
                    return true;
                }
            }
            return false;
        }, pair.value);
    }
}


///Parse the query and enumerate key/values pair
/**
 * @param vpath path or virtual path which contains the query. The query must be behind
 * character '?'. Anything before this character is ignored
 * @param fn function called for each pair
 */
template<std::invocable<std::string, std::string> Fn>
std::size_t parse_query_enum_kv(std::string_view vpath, Fn &&fn) {
    if (vpath.empty()) return 0;
    vpath = vpath.substr(std::min(vpath.length()-1, vpath.find('?'))+1);
    return parse_form_urlencoded_enum_kv(vpath, std::forward<Fn>(fn));
}

///Constructs QueryFieldMap from list of mapping items
/**
 *
 * @tparam T mandatory, you need to specify target type
 * @tparam N optional, specify count of items, if ommited, the compiler calculates
 * @param x list of items. It is expected N items. The each item contains pair wich defines
 *  mapping
 *
 * @code
 * constexpr auto mapping = makeQueryFieldMap<T>({
 *    {"foo", &T::foo},   //"foo" -> &T::foo
 *    {"bar", &T::bar},   //"bar" -> &T::bar
 * })
 * @endcode
 *
 * The function is declared as constexpr. Use this for benefit, as the mapping table
 * is generated during compile time.
 *
 * @return an instance of QueryFieldMap
 */
template<typename T, int N>
inline constexpr auto makeQueryFieldMap(const typename QueryFieldMap<T,N>::Item (&x)[N]) {
    return QueryFieldMap<T, N>(x);
}




}


}




#endif /* SRC_USERVER_HTTP_COMMON_H_ */

