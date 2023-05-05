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


    ///Generator-like object which implements websocket handshake on the client side
    /**
     * As generator-like object, it only handles one request at time, so avoid
     * to use this object shared between threads. It is easier to construct this
     * object in each handler instance. Object doesn't allocate any memory.
     *
     * You must keep it valid until the handshake is done
     */
    class Client {
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

       ///Construct the object
       /**
        * @param cfg configuration
        */
        Client(Config cfg):_cfg(cfg), _awt(*this) {}


        ///Perform websocket handshake on given client request
        /**
         * @param client client request. You can pass additional headers as you need. However
         * avoid to pass websocket specifics headers need for minimal hanshake (connection,
         * upgrade, Sec-Websocket-Key and Sec-Websocket-Version)
         *
         * @return the future is resolved by new websocket connection. Once you have
         * such connection, you can drop the original request. If the connection
         * is not established - for example it was rejected - return value
         * is future without a value. Direct access to the result throws
         * an exception await_canceled_exception(). In this case
         * check the request, status code, etc
         *
         * @exception cocls::await_canceled_exception - handshake failed
         */
        cocls::future<ws::Stream> operator()(http::ClientRequest &client);

    protected:
        Config _cfg;
        cocls::suspend_point<void> after_send(cocls::future<_Stream> &f) noexcept;

        http::ClientRequest *_req = nullptr;
        cocls::promise<ws::Stream> _result;
        cocls::call_fn_future_awaiter<&Client::after_send> _awt;
        std::string _digest;

    };



}


}



#endif /* SRC_USERVER_HTTP_WS_SERVER_H_ */
