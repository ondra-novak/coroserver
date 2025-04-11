#include "http_common.hpp"
#include <string>
#include <vector>
#include <sstream>
#include <filesystem>

namespace coroserver {
    namespace http {

     std::string normalize_uri(std::string_view uri) {
        std::vector<std::string> segments;
        std::istringstream stream((std::string(uri)));
        std::string segment;
    
        while (std::getline(stream, segment, '/')) {
            if (segment == "..") {
                if (!segments.empty()) {
                    segments.pop_back();
                }
            } else if (!segment.empty() && segment != ".") {
                segments.push_back(segment);
            }
        }
    
        std::ostringstream normalized_uri;
        for (const auto& seg : segments) {
            normalized_uri << "/" << seg;
        }
    
        return normalized_uri.str();
    }  

    std::optional<std::filesystem::path> map_uri_to_path(std::filesystem::path base_path, std::string_view uri) {
        std::string buff;
        unsigned int accum = 0;
        unsigned int s = 0;
        std::optional<std::filesystem::path> path (base_path);
        for (char c: uri) {
            if (s > 0) {
                unsigned int v = 0;
                if (c >= '0' && c <='9') v = v * 16 + c - '0';
                else if (c >= 'A' && c <='F') v = v * 16 + c - 'A' + 10;
                else if (c >= 'a' && c <='f') v = v * 16 + c - 'a' + 10;
                accum = (accum << 4) + v;
                if (--s == 0) buff.push_back(static_cast<char>(accum));
            }else if (c == '%') {
                s = 2;
            } else if (c == '+') {
                buff.push_back(' ');                
            } else if (c == '/') {
                auto part = std::string_view(buff);
                if (part.empty() || part == ".") {
                    buff.clear();
                    continue;
                }
                if (part == "..") {
                    if (path->has_parent_path()) path = path->parent_path(); 
                    else {
                        path.reset();
                        return path;                    
                    }
                    buff.clear();
                    continue;
                }
                (*path) /= part;
                buff.clear();
            } else if (c == '?') {
                break;
            } else {
                buff.push_back(c);
            }
        }
        if (!buff.empty()) {
            (*path) /= buff;
        }

        const auto &native_base_path = base_path.native();
        const auto &native_path = path->native();
        
        if (native_path.size() < native_base_path.size() ||
            native_path.compare(0, native_base_path.size(), native_base_path) != 0) {
            path.reset();
        }
        return path;
    }



}


}