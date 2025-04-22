#include "websocket_server.hpp"

namespace coroserver {

namespace ws {


awaitable<WebSocketStream> accept(http::ServerRequest &req, bool need_fragmented) {

    //request must be upgrade, otherwise it is not websocket
    if (!req.is_upgrade()) co_return std::nullopt;

    auto upgrade_type = req.get_header("Upgrade");
    auto sec_websocket_key = req.get_header("Sec-Websocket-Key");   
    //check whether the request is valid
    //if not, return empty optional
    //if the request is not valid, the request is not closed
    
    if (!upgrade_type.has_value() || !sec_websocket_key.has_value() || http::HeaderKey(*upgrade_type) != "upgrade") {
        co_return std::nullopt;
    }

    //calculate accept key
    auto accept_key = calculate_ws_accept(*sec_websocket_key);
    //set headers
    req.set_header("Upgrade", "websocket");
    req.set_header("Connection", "Upgrade");
    req.set_header("Sec-WebSocket-Accept", accept_key);
    req.set_header("Sec-WebSocket-Version", "13");
    //set status
    req.set_status(101);
    //send response. Because it is upgrade request, returned stream contains
    //original request stream which can be passed to WebSocketStream instance
    //However if send fails, returns empty value, other side probably closed connection prematurely
    auto s = co_await req.send().as_optional();
    if (!s.has_value()) {
        co_return std::nullopt;
    }
    //construct and return WebSocketStream ready to be used
    co_return WebSocketStream(std::move(*s), true, need_fragmented);
   

}


}
}