#pragma once
#ifndef SRC_COROSERVER_UMQ_PEER_H_
#define SRC_COROSERVER_UMQ_PEER_H_
#include "umq_connection.h"

#include "static_lookup.h"
#include <queue>
#include <cocls/shared_future.h>
namespace coroserver {

namespace umq {

class UMQException;

class Peer: std::enable_shared_from_this<Peer> {
public:

    ///State of the peer connection
    enum class State {
        ///Peer object is not bound to any connection
        unbound,
        ///Connected to peer performing initial handshake
        opening,
        ///Connection opened and full operational
        open,
        ///Sent request to close, but this was not yet confirmed from other side
        closing,
        ///Connection has been closed
        closed,
    };

    enum class ErrorCode {
        ok = 0,
        unsupported_version,
        invalid_message,
        unexpected_attachment,
        internal_error,
    };

    static constexpr std::string_view strVersion = "1.0.0";

    static constexpr auto strErrorCode = makeStaticLookupTable<ErrorCode, std::string_view>({
        {ErrorCode::ok, "OK"},
        {ErrorCode::unsupported_version, "Unsupported version"},
        {ErrorCode::invalid_message, "Invalid message"},
        {ErrorCode::unexpected_attachment, "Unexpected attachment"},
        {ErrorCode::internal_error, "Internal error"},
    });

    static UMQException makeError(ErrorCode code);

    struct Payload {
        std::string_view text;
        std::vector<coro::shared_future<std::vector<char> > > attachments;
    };

    struct HelloMessage: Payload {
        coro::promise<Payload> _response;
        void accept();
        void accept(std::string_view payload);
        void accept(Payload payload);
    };

    struct RPCRequest : Payload {

    };

    Peer();

    coro::future<HelloMessage> start_server(PConnection conn);

    coro::future<Payload> start_client(PConnection conn, Payload hello = {});


    State get_state() const;

    void close();
    void close(const UMQException &error);
    void close(ErrorCode code);

protected:

    enum class Type: char {
        ///global errors, empty id
        /**Last message in stream, always leads to closing connection */
        error = '!',
        ///Count of attachments
        /**every command with payload can have this prefix. Instead ID is number
         * count of expected attachments. The command starts at payload section
         *
         * @code
         * A1
         * Mabc
         * method.name
         * arguments
         * <binary attachment>
         * @endcode
         */
        attachment_count='A',
        ///Attachment error. Can appear instead binary content but counts as binary content
        /** id is empty, payload contains error message */
        attachment_error='e',
        ///Hello message Sends by an initiator. ID contains version, payload is accessible by respondent
        hello = 'H',
        ///Welcome message, sends as reply by respondent, ID contains version, payload is response
        welcome = 'W',
        ///RPC method call
        /**
         * Call RPC method, ID contains unique string. In the payload, there is name
         * of the method terminated by line separator, following data are arguments
         *
         * @code
         * Mabc
         * method-name
         * arguments
         * @endode
         */
        method = 'M',

        ///Response with callback
        /**
         * Callback works similar as method 'M' However, callbacks are 'one shot' while
         * methods are permanent.
         */
        result_callback = 'C',
        ///Result of method or callback
        /**
         * ID must be equal to to request 'M' or 'C'
         * the payload contains response. This message causes, that server expects
         * response from the client. The client must generate C, R, or E message
         * to continue in dialog.
         *
         * @code
         * a -> M -> b
         * a <- C <- b
         * a -> C -> b
         * a <- C <- b
         * a -> R -> b
         * @endcode
         */
        result = 'R',
        ///Exception of method
        /**
         * Exception is error response of the method. It means that method has been
         * found, called, but failed. Payload contains error message
         * ID must be equal to id of the request or the callback
         */
        exception = 'E',
        ///Failed to route to target method
        /**
         * Method not found, or cannot be routed (network error, service unavailable),
         * This means, that method was not called
         * Payload contains error message
         * ID must be equal to id of the request or the callback
         */
        route_error = '/',
        /// Topic (subscription) update
        /**
         * ID contains id of topic
         * payload contains data of the update
         */
        topic_update = 'T',
        /// Request to unsubscribe
        /**
         * Send by subscriber to publisher to stop generating topic_update messages
         * ID contains id of topic
         * payload is empty
         */
        unsubscribe = 'U',
        ///Topic expired or has been closed
        /**
         * Send by publisher notification that no more messages will be generated for
         * given topic
         * ID contains id of topic
         * payload is empty
         *
         * It is not error to generate this message as response to a message 'unsubscribe'.
         */
        topic_close = 'Z',
        ///Set/update remote variable
        /**
         * ID = name of variable
         * payload - value of the variable
         */
        set_var = 'S',
        ///Unset the remove variable
        /**
         * ID = name of variable
         * patload is empty
         */
        unset_var = 'X',
        ///Discover services offered by the peer
        /**
         * It is like 'M' but only returns description
         * @code
         * ?<id>
         * method.name
         * @endcode
         * Result is help for this method.
         *
         * If sends with empty payload or with name of route,
         * the list of methods is returned. This list is formatted by following
         * way
         *   \M<name> - method
         *   \R<name> - route
         *
         */
        discover = '?'


    };

    PConnection _conn;

    coro::future<void> _close_future;
    coro::promise<void> _close_promise;
    std::atomic_flag _closing;
    bool _closed = false;

    bool send(Type type, std::string_view id = {}, std::string_view payload = {}, int attachments = 0);
    bool send(Type type, std::string_view id = {}, const Payload &pl = {});
    void start_reader();
    coro::suspend_point<void> on_message(coro::future<Message> &f) noexcept;
    bool recvTextMessage(std::string_view message);
    bool recvBinaryMessage(std::string_view message);

    void allocate_attachments(std::string_view txtcount, std::vector<coro::shared_future<std::vector<char> > > &attach);

    coro::call_fn_future_awaiter<&Peer::on_message> _on_msg_awt;

    std::queue<coro::promise<std::vector<char> > > _awaited_binary;
    std::queue<coro::shared_future<std::vector<char> > > _attach_send_queue;
    std::mutex _attach_send_queue_mx;
    coro::suspend_point<void> on_attachment_ready(coro::awaiter *) noexcept;
    coro::call_fn_awaiter<&Peer::on_attachment_ready> _att_ready;
    std::shared_ptr<Peer> _att_ready_peer_ref;


    void on_attachment_error(const UMQException &e);
    void on_hello_message(const std::string_view &version, const Payload &payload);
    void on_welcome_message(const std::string_view &version, const Payload &payload);


    coro::promise<Payload> _welcome_response;
    coro::promise<HelloMessage> _hello_request;


};



class UMQException:  public std::exception {
public:
    UMQException(int code, std::string_view message);
    UMQException(std::string_view txtmsg);

    std::string_view get_serialized() const;
    const char *what() const noexcept {return _msgtext.c_str();}
protected:
    std::string _msgtext;
    int _code;
    std::string_view _message;
};

}


}



#endif /* SRC_COROSERVER_UMQ_PEER_H_ */
