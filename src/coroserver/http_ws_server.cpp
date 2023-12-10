/*
 * http_ws_server.cpp
 *
 *  Created on: 17. 11. 2022
 *      Author: ondra
 */

#include "http_ws_server.h"
#include "http_stringtables.h"
#include "sha1.h"
#include "strutils.h"
namespace coroserver {

namespace ws {


 coro::future<bool> Server::operator()() {
    return [&](auto result) {
        if (!_req.allow({http::Method::GET})
          || _req[http::strtable::hdr_upgrade] != http::HeaderValue(http::strtable::val_websocket)
          || _req[http::strtable::hdr_connection] != http::HeaderValue(http::strtable::val_upgrade)) {
            result(false);
            return; //drops promise
        }

        std::string_view key = _req["Sec-WebSocket-Key"];
        if (key.empty()) {
            result(false);
            return;
        }


        SHA1 sha1;
        sha1.update(key);
        sha1.update("258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
        std::string digest = sha1.final();
        std::string digestResult;
        digestResult.reserve((digest.size()*4 + 3)/3);
        base64::encode(digest, [&](char c){digestResult.push_back(c);});

        _req.set_status(101);
        _req("Upgrade","websocket");
        _req("Connection","Upgrade");
        _req("Sec-WebSocket-Accept",digestResult);
        _result = std::move(result);
        _awt << [&]{return _req.send();};
    };
}

coro::suspend_point<void> Server::on_response_sent(coro::future<_Stream> &sfut) noexcept {
    try {
        auto s = *sfut;
        s.set_timeouts(_tm);
        _out = Stream(s,{false, _need_fragmented});
        auto r = _result(true);
        return r;
    } catch (...) {
        auto r = _result(false);
        return r;
    }
}

}


}


