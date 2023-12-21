/*
 * http_ws.h
 *
 *  Created on: 17. 11. 2022
 *      Author: ondra
 */

#ifndef SRC_USERVER_HTTP_WS_SERVER_H_
#define SRC_USERVER_HTTP_WS_SERVER_H_

#include "websocket_stream.h"
#include "http_client.h"

#include <optional>
namespace coroserver {

namespace ws {



    class Client: public coro::future<Stream> {
    public:

        static Client connect(http::ClientRequest &req,
                              TimeoutSettings tm = defaultTimeout,
                              bool need_fragmented = false) {
            return Client(req, tm, need_fragmented);
        }


    protected:

        Client(http::ClientRequest &req, TimeoutSettings tm = defaultTimeout, bool need_fragmented = false);


        TimeoutSettings _tm;
        bool _need_fragmented;
        coro::promise<Stream> _result;
        coro::lazy_future<_Stream> _fut;
        coro::any_target<> _target;
        std::string _digest;


    };



}


}



#endif /* SRC_USERVER_HTTP_WS_SERVER_H_ */
