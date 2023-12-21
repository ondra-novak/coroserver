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


///The class performs websocket servers's handshake.
/**
 * @code
 *    coroserver::ws::Stream s = co_await coroserver::ws::Server::accept(request);
 * @endcode
 *
 * Object acts as coro::future<> returning websocket stream
 *
 */

class Server: public coro::future<Stream> {
public:

    ///Accept websocket connection
    /**
     * @param req HTTP server request (untouched)
     * @param tm  timeouts set to final stream
     * @param need_fragmented specify true, if you need fragmented messages. It disables
     *   concatelating of continuous frames. Default is false, so messages will not
     *   be fragmented.
     * @return return object, which acts as coro::future. You can co_await on it. The
     * function returns websocket stream, if the request is accepted. If the request
     * cannot be accepted (invalid request), the exception coro::broken_promise_exception is
     * thrown
     *
     *
     * @exception coro::broken_promise_exception - invalid request
     */
    static Server accept(http::ServerRequest &req, TimeoutSettings tm = defaultTimeout, bool need_fragmented = false) {
        return Server(req, tm, need_fragmented);
    }


protected:

    Server(http::ServerRequest &req,
           TimeoutSettings tm = defaultTimeout,
           bool need_fragmented = false)
        :_result(coro::future<Stream>::get_promise())
    {
        init(req, tm, need_fragmented);
    }


    coro::promise<Stream> _result;
    coro::future<_Stream> _fut;
    coro::any_target<> _target;

    bool _need_fragmented;
    TimeoutSettings _tms;

    void init(http::ServerRequest &req, TimeoutSettings tm, bool need_fragmented);


};


}


}



#endif /* SRC_USERVER_HTTP_WS_SERVER_H_ */
