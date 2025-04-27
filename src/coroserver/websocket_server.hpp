#pragma once

#include "websocket_stream.hpp"
#include "http_server_request.hpp"


namespace coroserver {

namespace ws {


///accept websocket server connection
/**
 * @param req http server request containing upgrade header
 * @param need_fragmented set true, to enable fragmented messages. This is
 * useful, if the reader requires to stream messages. Default is false,
 * when fragmented message is received, it is completed and returned as whole
 *
 * @return awaitable carrying WebSocketStream. If the request cannot be accepted,
 * the function cancels await operation. You can use awaitable::as_optional() to
 * check whether the request was accepted or not.
 *
 * In case that request is not accepted, you should explore request to determine,
 * what was wrong. The request is not closed, so you can use it to send error response.
 */
awaitable<WebSocketStream> accept(http::ServerRequest &req, bool need_fragmented = false);


}

}
