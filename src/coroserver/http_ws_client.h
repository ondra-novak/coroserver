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


    ///The class performs websocket client's handshake.
    /**
     * To perform the handshake, the class requires an open
     * http::ClientRequest object to the target server and
     * allows for additional headers to be added as needed.
     * The class modifies the request, sends it to the server,
     * and parses the response headers. If the headers meet
     * the requirements for a valid WebSocket handshake,
     * the class creates a WebSocket stream as ws::Stream.
     * If the headers are invalid, the class reports an error.
     *
     * The class is designed to be used asynchronously within a coroutine,
     * and it automatically persists the its instance (state) during
     * the handshake process. The only thing you need to do is
     * to co_await the result of the Client::connect method.
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
    class Client {
    public:

        ///Create state object to begin websocket's handshake
       /**
        * Creates a new object for initiating the WebSocket handshake with a server.
        * The constructor takes several parameters:
        *
        * @param out A reference to a variable of type `ws::Stream`
        *        that receives the opened WebSocket stream
        *        if the handshake is successful.
        * @param req The request object to be used for the handshake.
        *         The request must be opened with a `GET` method to
        *         a specific URL. After the handshake is complete,
        *         this object can be dropped as it is no longer needed.
        * @param tm The WebSocket stream timeouts, where the read timeout
        *          defines the ping interval and the write timeout defines
        *          the period after which the WebSocket stream is considered
        *          disconnected if it is stuck on writing.
        * @param need_fragmented A boolean flag indicating whether
        *         you need to receive fragmented messages
        *         (e.g., in streaming scenarios) or if you want
        *         to combine all fragments into one message.
        *
        * The constructor returns a new object that can be used
        * to continue the handshake process by calling other
        * methods defined on the interface.
        *
        * @note The state object created by this constructor should
        * be kept alive during the entire handshake process. After
        * the handshake is complete, the object should be destroyed.
        */
        static Client connect(Stream &out,
                              http::ClientRequest &req,
                              TimeoutSettings tm = {60000,60000},
                              bool need_fragmented = false) {
            return Client(out, req, tm, need_fragmented);
        }

        ///"Call" the state, which initiates the handshake
        /**
         * @return a future object. which eventually resolves with result
         * of the handshake operation
         * @retval true handshake successful, you can start to use websocket stream
         * @retval false handshake failed, stream is not defined, you need to
         * check request
         */
        coro::future<bool> operator()();


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
        coro::future_awaiter<bool> operator co_await() {
            return [&]{return (*this)();};
        }

        ///Perform synchronous handshake and wait (blocking call) on result
        /**
         * @retval true handshake successful, you can start to use websocket stream
         * @retval false handshake failed, stream is not defined, you need to
         */
        bool wait() {
            return operator()().wait();
        }


    protected:

        Client(Stream &out, http::ClientRequest &req, TimeoutSettings tm = {60000,60000}, bool need_fragmented = false)
            :_out(out), _req(req), _tm(tm), _need_fragmented(need_fragmented), _awt(*this) {}

        Stream &_out;
        coro::suspend_point<void> after_send(coro::future<_Stream> &f) noexcept;
        http::ClientRequest &_req;
        TimeoutSettings _tm;
        bool _need_fragmented;
        coro::promise<bool> _result;
        coro::call_fn_future_awaiter<&Client::after_send> _awt;
        std::string _digest;


    };



}


}



#endif /* SRC_USERVER_HTTP_WS_SERVER_H_ */
