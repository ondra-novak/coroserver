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

    std::filesystem::path map_uri_to_path(std::filesystem::path base_path, std::string_view uri) {
        uri = split_at(uri, "?");
        if (uri.empty()) return base_path;
        auto path = normalize_uri(uri);
        if (path.empty()) return base_path;
        if (path.front() == '/') path = path.substr(1);
        return base_path / path;
    }



}


}