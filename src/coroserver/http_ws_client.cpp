/*
 * http_ws_server.cpp
 *
 *  Created on: 17. 11. 2022
 *      Author: ondra
 */

#include "http_ws_client.h"

#include "sha1.h"

#include "base64.h"
namespace userver {

namespace ws {

static void generate_key_and_digest(std::string &key, std::string &digest) {
    std::uint8_t rndkey[16];
    std::random_device rnd;
    for (auto &x: rndkey) {
        x = static_cast<std::uint8_t>(rnd() & 0xFF);
    }
    key.clear();
    key.reserve(32);
    base64encode(std::string_view(reinterpret_cast<const char *>(rndkey), 16)
            , [&](char c){
        key.push_back(c);
    });

    digest.clear();

    SHA1 sha1;
    sha1.update(key);
    sha1.update("258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    std::string digestBin = sha1.final();
    std::string digestResult;
    digest.reserve((digestBin.size()*4 + 3)/3);
    base64encode(digestBin, [&](char c){digest.push_back(c);});

}


cocls::task<ConnectResult>connect(http::Client::Request req ) {
    std::string key;
    std::string digest;
    generate_key_and_digest(key, digest);
    req->header(http::strtable::hdr_upgrade,http::strtable::val_websocket);
    req->header(http::strtable::hdr_connection,http::strtable::val_upgrade);
    req->header("Sec-WebSocket-Key", key);
    req->header("Sec-WebSocket-Version", 13);
    co_await req->send();
    if (req->status() == 101 && req->header("Sec-WebSocket-Accept") == std::string_view(digest)) {
        GenStream s (req->steal_stream());
        co_return ConnectResult(Stream(std::move(s),true), std::move(req));
    } else {
        co_return ConnectResult({}, std::move(req));
    }
}

}


}


