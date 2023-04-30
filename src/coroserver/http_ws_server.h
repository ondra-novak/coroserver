/*
 * http_ws.h
 *
 *  Created on: 17. 11. 2022
 *      Author: ondra
 */

#ifndef SRC_USERVER_HTTP_WS_SERVER_H_
#define SRC_USERVER_HTTP_WS_SERVER_H_

#include "websocket_stream.h"
#include "http_server_request.h"

#include <optional>
namespace coroserver {

namespace ws {

///Implements websocket server, implementing server side of websocket handshake
/**
 * To use this class, you need to create an instance in a http handler. It
 * is expected, that handler is coroutine otherwise it is required to
 * keep instance on live until the handshake is finished
 *
 * The single instance can handle the handshake for a single connection. The object
 * is not MT Safe.
 *
 * (Instance works similar as generator, it is called with request and returns
 * stream - during this period, no other connection can be accepted)
 */
class Server {
public:

    ///Configuration values
    struct Config {
        ///Setup stream timeouts, it is recommended to have this values
        /**
         * Read timeout also specifies ping interval. If the read timeouts,
         * the ping is send, and next interval is measured. So after two timeouts
         * without response, the connection is closed
         *
         * Write timeout works as expected. If timeout happen during the write,
         * the connection is closed
         */
        TimeoutSettings io_timeout = {60000,60000};
        ///Set true, if you need fragmented messages, set false to join all fragments to one message
        bool need_fragmented = false;
    };

    ///Configure server
    Server(Config cfg):_cfg(cfg),_awt(*this) {}
    ///Accept the websocket connection
    /**
     * @param req http server request. You should check headers you need before you call
     * this function
     *
     * @return websocket Stream, if handshake were successful, or future with no value
     * if handshake failed.
     *
     * @code
     * Server server({});
     * auto res = server.accept(request)
     * if (co_await res.has_value()) {
     *      Stream stream = *res;
     *      // .. continue with stream
     * } else {
     *      req.set_status(400);
     * }
     * @endcode
     */
    cocls::future<Stream> accept(http::ServerRequest &req);

protected:
    Config _cfg;
    std::atomic_flag _busy;
    cocls::suspend_point<void> on_response_sent(cocls::future<_Stream> &s) noexcept;

    cocls::promise<Stream> _result;
    cocls::call_fn_future_awaiter<&Server::on_response_sent> _awt;
};


}


}



#endif /* SRC_USERVER_HTTP_WS_SERVER_H_ */
