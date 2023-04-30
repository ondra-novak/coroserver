/*
 * http_ws.h
 *
 *  Created on: 17. 11. 2022
 *      Author: ondra
 */

#ifndef SRC_USERVER_HTTP_WS_SERVER_H_
#define SRC_USERVER_HTTP_WS_SERVER_H_

#include "ws_stream.h"
#include "http_client.h"

#include <optional>
namespace userver {

namespace ws {

    class ConnectResult {
    public:
        ConnectResult(std::optional<ws::Stream> stream, http::Client::Request response)
            :_stream(std::move(stream)), _response(std::move(response)) {}

        bool connected() const {return _stream.has_value() ;}
        ws::Stream &stream() {return *_stream;}
        http::Client::Request &response() {return _response;}
    protected:
        std::optional<ws::Stream> _stream;
        http::Client::Request _response;
    };

    ///Connect websocket server
    /**
     * @param req request, must be opened through http::Client for given url and method GET
     * You can add custom headers before the request is passed to the function
     *
     * @return object which can have two states. You can check successful connect by
     * ConnectResult::connected(). In case of true response, you can get ws::Stream
     *  by ConnectResult::stream() otherwise, you can retrieve response from the server
     *  by ConnectResult::response(), where you can read status code and other
     *  headers
     */
    cocls::task<ConnectResult> connect(http::Client::Request req );

}


}



#endif /* SRC_USERVER_HTTP_WS_SERVER_H_ */
