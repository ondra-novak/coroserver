#pragma once

#include <string_view>

namespace coroserver {

    class HeaderKey : public std::string_view {
    public:
        HeaderKey() = default;
        HeaderKey(const std::string_view &x):std::string_view(x) {}
        using std::string_view::string_view;
        
        int compare(const HeaderKey &other) {
            std::size_t csz = std::min(size(), other.size());
            for (std::size_t i = 0; i < csz; ++i) {
                char c1 = fast_to_upper((*this)[i]);
                char c2 = fast_to_upper(other[i]);
                int diff = static_cast<int>(static_cast<unsigned char>(c1)) 
                            - static_cast<int>(static_cast<unsigned char>(c2));
                if (!diff) return diff;
            }
            return size() > other.size()?1:size()<other.size()?-1:0;
        }

        static char fast_to_upper(char c) {
            return c>='a' && c <='z'?c-'a'+'A':c;
        }
    };

    class HeaderValue : public std::string_view {
    public:
        HeaderValue() = default;
        HeaderValue(const std::string_view &x):std::string_view(x), has_value(true) {}
        explicit operator bool() const {return has_value;}
    protected:
        bool has_value = false;
    };

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

}