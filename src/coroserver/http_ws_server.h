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
 * To perform the handshake, the class requires an open
 * http::ServerRequest object from a client
 * The class explore the request, and sends apropriate response
 * to the client. If the headers in the request meet
 * the requirements for a valid WebSocket handshake,
 * the class creates a WebSocket stream as ws::Stream.
 * If the headers are invalid, the class reports error and no
 * response is sent to the client.
 *
 * The class is designed to be used asynchronously within a coroutine,
 * and it automatically persists the its instance (state) during
 * the handshake process. The only thing you need to do is
 * to co_await the result of the Server::accept method.
 *
 * However, if you're not using coroutines, you will need to
 * manually create and maintain the state during the handshake
 * process. You need to "call" the state (as function) to
 * initiate the handshake and keepi the state
 * alive until the handshake is complete.
 *
 * Alternatively, you can use the wait() function to perform
 * synchronous waiting.
 *
 * It's important to note that this object is not reusable
 * and should be destroyed after the handshake is complete.
 *
 */

class Server {
public:

    ///Create state object to begin websocket's handshake
    /**
    * Creates a new object for responding the WebSocket handshake with a client.
    * The constructor takes several parameters:
    *
    * @param out A reference to a variable of type `ws::Stream`
    *        that receives the opened WebSocket stream
    *        if the handshake is successful.
    * @param req The request object to be used for the handshake.
    *         The request must be opened with a `GET` . After the
    *         handshake is complete, this object can be dropped
    *         as it is no longer needed.
    * @param tm The WebSocket stream timeouts, where the read timeout
    *          defines the ping interval and the write timeout defines
    *          the period after which the WebSocket stream is considered
    *          disconnected if it is stuck on writing.
    * @param need_fragmented A boolean flag indicating whether
    *         you need to receive fragmented messages
    *         (e.g., in streaming scenarios) or if you want
    *         to combine all fragments into one message.
    *
    * The constructor returns a new object that can be used to continue
    * the handshake process by calling other methods defined on the interface.
    *
    * @note The state object created by this constructor should
    * be kept alive during the entire handshake process.
    * After the handshake is complete, the object should be destroyed.
    */
    static Server accept(Stream &out,http::ServerRequest &req, TimeoutSettings tm = {50000,50000}, bool need_fragmented = false) {
        return Server(out,req, tm, need_fragmented);
    }

    ///"Call" the state, which initiates the handshake
    /**
     * @return a future object. which eventually resolves with result
     * of the handshake operation
     * @retval true handshake successful, you can start to use websocket stream
     * @retval false handshake failed, stream is not defined, you need to
     * check request
     */
    cocls::future<bool> operator()();

    ///Allows to co_await on the state.
    /**
     * The current coroutine is suspended and it is eventually resumed
     * when handshake is finished.
     *
     * @retval true handshake successful, you can start to use websocket stream
     * @retval false handshake failed, stream is not defined, you need to
     * check server's response stored in the http::ClientRequest object passed
     * as argument in the constructor
     */
    cocls::future_awaiter<bool> operator co_await() {
        return [&]{return (*this)();};
    }

    bool wait() {
        return operator()().wait();
    }

protected:

    Server(Stream &out,
           http::ServerRequest &req,
           TimeoutSettings tm = {60000,60000},
           bool need_fragmented = false)
        :_out(out)
        ,_need_fragmented(need_fragmented)
        ,_tm(tm)
        ,_req(req)
        ,_awt(*this){}

    Stream &_out;
    bool _need_fragmented;
    TimeoutSettings _tm;
    http::ServerRequest &_req;
    cocls::suspend_point<void> on_response_sent(cocls::future<_Stream> &s) noexcept;

    cocls::promise<bool> _result;
    cocls::call_fn_future_awaiter<&Server::on_response_sent> _awt;
};


}


}



#endif /* SRC_USERVER_HTTP_WS_SERVER_H_ */
