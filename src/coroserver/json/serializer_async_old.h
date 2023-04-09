#pragma once
#ifndef SRC_USERVER_JSON_SERIALIZE_ASYNC_H_
#define SRC_USERVER_JSON_SERIALIZE_ASYNC_H_

#include "serializer_old.h"

#include "../async.h"

#include <array>
namespace userver {

namespace json {


///Creates generator, which is able to serialize json asynchronously
/**
 * @tparam buffer_size size of internal buffer. It generates chunks of
 * this size.
 *
 * @param v json to serialize
 * @return generator which returns std::string_view of each chunk.
 */
template<std::size_t buffer_size = 65536>
generator<std::string_view> serialize(Value v) {
    std::array<char, buffer_size> buffer;
    Serializer srl(v);
    auto res = srl.serialize(buffer.begin(), buffer.end());
    while (res != buffer.begin()) {
        auto sz = std::distance(buffer.begin(), res);
        co_yield std::string_view(buffer.data(), sz);
        res = srl.serialize(buffer.begin(), buffer.end());
    }
    co_return;
}

template<typename ServerRequest, std::size_t buffer_size = 65536>
future<void> send_json_http(Value v, ServerRequest &req) {
    req.header("Content-Type","application/json");
    return req.send(json::serialize<buffer_size>(std::move(v)));
}


}

}



#endif /* SRC_USERVER_JSON_SERIALIZE_ASYNC_H_ */
