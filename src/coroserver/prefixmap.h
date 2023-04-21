/*
 * pathmap.h
 *
 *  Created on: 21. 4. 2023
 *      Author: ondra
 */

#ifndef SRC_COROSERVER_PREFIXMAP_H_
#define SRC_COROSERVER_PREFIXMAP_H_
#include <array>
#include <string>
#include <unordered_map>

namespace coroserver {


///Creates prefix map
/**
 * @tparam T type of value associated with prefix
 *
 * Prefix map searches for common prefixes.
 */

template<typename T>
class PrefixMap {
public:

    struct Value {
        std::string path;
        T payload;
    };

    using Hash = std::size_t;


    using Map = std::unordered_multimap<Hash, Value>;

    static auto fnv1a() {
        if constexpr(sizeof(std::size_t) >= 8) {
            return [hash = uint64_t(14695981039346656037ULL)](char c) mutable {
                hash ^= static_cast<uint64_t>(c);
                hash *= 1099511628211ULL;
                return std::size_t(hash);
            };
        } else {
            return [hash = uint32_t(2166136261U)](char c) mutable {
                hash ^= static_cast<uint32_t>(c);
                hash *= 16777619U;
                return std::size_t(hash);
            };

        }
    }

    ///Insert string and value
    /**
     * @param s string which serves as prefix
     * @param payload value associated with prefix
     */
    void insert(std::string s, T payload) {
        auto hash = fnv1a();
        std::size_t h = 0;
        for (char c: s) h = fnv1a(c);
        _map.emplace(h, Value {s, payload});
        _maxlen = std::max(_maxlen, s.length());
    }

    ///Contains result
    /** Can contain up 10 nodes with common prefixes. It is sorted from
     * shorted to longest. It can be used also as stack. In this case, top
     * item is longest.
     */
    struct Result {
        std::array<const Value *, 10> _nodes;
        int _count = 0;
        auto begin() const {return _nodes.begin();}
        auto end() const {
            auto b = begin();
            std::advance(b, _count);
            return b;
        }
        void push(const Value &v) {
            if (_count < 10) _nodes[_count++] = &v;
        }
        bool empty() const {return _count == 0;}
        const Value &top() const {return *_nodes[_count];}
        void pop() {
            --_count;
        }
    };


    ///Search common prefixes
    /**
     *
     * @param s string to find
     * @return up to 10 common prefixes with their values
     */
    Result find(std::string_view s) const {
        auto hash = fnv1a();
        Result res;

        auto update_res = [&](std::pair<typename Map::const_iterator, typename Map::const_iterator> r, std::string_view ss) {
            for (typename Map::const_iterator iter = r.first; iter != r.second; ++iter) {
                const Value &v = iter->second;
                if  (ss == v.path) {
                    res.push_back(v);
                }
            }
        };

        std::size_t slen = std::min(s.length(), _maxlen);
        for (std::size_t i = 0; i < slen; i++) {
            char c = s[i];
            auto h = hash(c);
            auto r = _map.equal_range(h);
            if (r.first != r.second) update_res(r, s.substr(0, i));
        }
    }

protected:
    Map _map;
    std::size_t _maxlen = 0;
};



}



#endif /* SRC_COROSERVER_PREFIXMAP_H_ */
