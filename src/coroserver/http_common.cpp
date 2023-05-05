#include "http_common.h"
#include "http_stringtables.h"
#include "strutils.h"
#include <algorithm>

namespace coroserver {

namespace http {


ContentType extensionToContentType(const std::string_view &txt) {
    return ext2ctx[txt];
}



bool HeaderMap::headers(const std::string_view hdrstr, HeaderMap &hdrmap, std::string_view &firstLine) {
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
