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

void Server::init(http::ServerRequest &req, TimeoutSettings tm, bool need_fragmented) {
    if (!req.allow({http::Method::GET})
      || req[http::strtable::hdr_upgrade] != http::HeaderValue(http::strtable::val_websocket)
      || req[http::strtable::hdr_connection] != http::HeaderValue(http::strtable::val_upgrade)) {
        _result.drop();
        return; //drops promise
    }

    std::string_view key = req["Sec-WebSocket-Key"];
    if (key.empty()) {
        _result.drop();
        return;
    }


    SHA1 sha1;
    sha1.update(key);
    sha1.update("258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    std::string digest = sha1.final();
    std::string digestResult;
    digestResult.reserve((digest.size()*4 + 3)/3);
    base64::encode(digest, [&](char c){digestResult.push_back(c);});

    req.set_status(101);
    req("Upgrade","websocket");
    req("Connection","Upgrade");
    req("Sec-WebSocket-Accept",digestResult);
    _need_fragmented = need_fragmented;
    _tms = tm;
    _fut << [&]{return req.send();};
    _fut.register_target(_target.call([&](auto *fut){
        try {
            _Stream s = *fut;
            s.set_timeouts(_tms);
            this->_result(s,Stream::Cfg{false, _need_fragmented});
        } catch (...) {
            this->_result.reject();
        }

    }));
}

}



}


