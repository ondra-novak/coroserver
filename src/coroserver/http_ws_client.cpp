/*
 * http_ws_server.cpp
 *
 *  Created on: 17. 11. 2022
 *      Author: ondra
 */

#include "http_ws_client.h"
#include "http_stringtables.h"

#include "sha1.h"
#include "strutils.h"

namespace coroserver {

namespace ws {


static void generate_key_and_digest(std::string &key, std::string &digest) {
    std::uint8_t rndkey[16];
    std::random_device rnd;
    for (auto &x: rndkey) {
        x = static_cast<std::uint8_t>(rnd() & 0xFF);
    }
    key.clear();
    key.reserve(32);
    base64::encode(std::string_view(reinterpret_cast<const char *>(rndkey), 16)
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
    base64::encode(digestBin, [&](char c){digest.push_back(c);});

}

cocls::future<bool> Client::operator()() {
    return [&](auto promise) {
        _result= std::move(promise);
        std::string key;
        generate_key_and_digest(key, _digest);
        _req(http::strtable::hdr_upgrade,http::strtable::val_websocket);
        _req(http::strtable::hdr_connection,http::strtable::val_upgrade);
        _req("Sec-WebSocket-Key", key);
        _req("Sec-WebSocket-Version", 13);
        _awt << [&]{return _req.send();};
    };
}

cocls::suspend_point<void> Client::after_send(cocls::future<_Stream> &f) noexcept {
    try {
        _Stream s(std::move(*f));
        if (_req.get_status() == 101 && _req["Sec-WebSocket-Accept"] == std::string_view(_digest)) {
            s.set_timeouts(_tm);
            _out = Stream(std::move(s),{true, _need_fragmented});
            return _result(true);
        } else {
            return _result(false);
        }
    } catch (...) {
        return _result(std::current_exception());
    }
}



}


}


