/*
 * http_query.h
 *
 *  Created on: 4. 11. 2022
 *      Author: ondra
 */

#ifndef SRC_USERVER_HTTP_QUERY_H_
#define SRC_USERVER_HTTP_QUERY_H_
#include "urlencdec.h"

#include "http_common.h"
#include "helpers.h"
#include <string_view>
#include <utility>
#include <vector>


using userver::url::decode;

namespace coroserver {

namespace http {

///Holds parsed query
/**
 * It can be generally used to hold any key-value structure
 *
 * To create this object, you need to use QueryBuilder
 *
 * Object can be reused to store different query, which
 * reduces count of allocations (because buffers are already allocated)
 *
 */
class Query;

///Prepares Query object
/**
 * Allows to add key-value pairs of strings, or parse query string of an url
 *
 * Object can be reused with advantage to reduce allocations
 */
class QueryBuilder {
public:

    using StrOfs = std::pair<std::size_t, std::size_t>;

    ///clear content without deallocating buffers
    void clear();
    ///builds Query object
    Query commit();
    ///rewrites content of Query object
    void commit(Query &q);
    ///adds key-value string
    /**
     * @param key key
     * @param value value
     */
    void add(const std::string_view &key, const std::string_view &value);

    ///parse url query string (text after ? till end of string)
    /**
     * @param str query string
     * Result is added to builder
     *
     * @note also performs url decoding of the encoded values
     */
    void parse_query_string(std::string_view str);




    ///Direct string value manipulation - add string, decode from url encoding
    /**
     * @param encoded url-encoded string in role as value
     * @return string reference
     *
     * @note used by Pathnode while resolving the path, as temporary storage for variables
     */
    StrOfs store_string_decode(const std::string_view &encoded);

    ///Direct string value manipulation - get stored string as view
    /**
     * @param ref reference to stored string, returns the view. View is valid until next string is stored
     * @return view to string
     * @note used by Pathnode while resolving the path, as temporary storage for variables
     */
    std::string_view get_stored_string(StrOfs ref) const;
    ///Delete stored string
    /**
     * You can delete string, which was recently added. You cannot delete string after other string was
     * added. However, you can remove string in reverse order of added string
     * @param ref reference to string
     * @retval true deleted
     * @retval false can't be deleted
     * @note used by Pathnode to delete values while backtracking from search
     *
     */
    bool delete_string(StrOfs ref) ;

    ///Add key-value where value is already stored in the QueryBuilderr
    /**
     *
     * @param key key name
     * @param ref reference to stored value
     */
    void add(const std::string_view &key, StrOfs ref);





protected:
    using KV = std::pair<StrOfs, StrOfs>;

    std::vector<char> _buffer;
    std::vector<KV> _items;
    StrOfs add_string(const std::string_view &txt);
    StrOfs begin_string();
    void end_string(StrOfs &ofs);

};




class Query {
public:
    ///Construct empty object
    Query() = default;
    ///clear content
    void clear();
    ///retrieve value of a key
    /**
     * @param name name of key
     * @return associated value. Returns undefined value if not defined
     */
    HeaderValue operator[](const std::string_view &name) const;
    decltype(auto) variables() const {
        return _items;
    }

protected:
    friend class QueryBuilder;
    std::vector<char> _texts;
    std::vector<std::pair<std::string_view, std::string_view> > _items;

};
inline void QueryBuilder::clear() {
    _buffer.clear();
    _items.clear();
}

inline void QueryBuilder::commit(Query &q) {
    q.clear();
    std::copy(_buffer.begin(), _buffer.end(), std::back_inserter(q._texts));
    for (KV &x: _items) {
        q._items.push_back({
            std::string_view(q._texts.data()+x.first.first,x.first.second),
            std::string_view(q._texts.data()+x.second.first,x.second.second)
        });
    }
    std::sort(_items.begin(), _items.end());
}

inline void QueryBuilder::add(const std::string_view &key, const std::string_view &value) {
    auto k = add_string(key);
    auto v = add_string(value);
    _items.push_back({k,v});
}

inline Query QueryBuilder::commit() {
    Query q;
    commit(q);
    return q;
}

inline QueryBuilder::StrOfs QueryBuilder::add_string(const std::string_view &txt) {
    StrOfs r{_buffer.size(), txt.size()};
    std::copy(txt.begin(), txt.end(), std::back_inserter(_buffer));
    return r;

}

inline QueryBuilder::StrOfs QueryBuilder::begin_string() {
    return {_buffer.size(),0};
}

inline QueryBuilder::StrOfs QueryBuilder::store_string_decode(const std::string_view &encoded) {
    auto ref = begin_string();
    if (encoded.empty()) return ref;
    url::decode(encoded, [&](char c){_buffer.push_back(c);});
    end_string(ref);
    return ref;
}

inline std::string_view QueryBuilder::get_stored_string(StrOfs ref) const{
    return std::string_view(_buffer.data()+ref.first, ref.second);
}

inline bool QueryBuilder::delete_string(StrOfs ref)  {
    if (ref.first+ref.second == _buffer.size()) {
        _buffer.resize(ref.first);
        return true;
    } else {
        return false;
    }
}

inline void QueryBuilder::add(const std::string_view &key, StrOfs ref) {
    auto k = add_string(key);
    _items.push_back({k, ref});
}

inline void QueryBuilder::end_string(StrOfs &ofs) {
    ofs.second = _buffer.size() - ofs.first;
}

inline void Query::clear() {
    _items.clear();
    _texts.clear();
}

inline void QueryBuilder::parse_query_string(std::string_view str) {
    while (!str.empty()) {
        auto value = splitAt("&",  str);
        auto key = splitAt("=", value);
        StrOfs kofs = begin_string();
        url::decode(key, [&](char c){_buffer.push_back(c);});
        end_string(kofs);
        StrOfs vofs = begin_string();
        url::decode(value, [&](char c){_buffer.push_back(c);});
        end_string(vofs);
        _items.push_back({kofs,vofs});
    }
}

inline HeaderValue Query::operator [](const std::string_view &name) const {
    auto iter = std::lower_bound(_items.begin(), _items.end(), std::pair(name, std::string_view()));
    if (iter == _items.end() || iter->first != name) return HeaderValue();
    else return HeaderValue(iter->second);
}

}




}


#endif /* SRC_USERVER_HTTP_QUERY_H_ */
