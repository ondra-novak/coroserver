#pragma once
#ifndef SRC_COROSERVER_UMQ_PEER_H_
#define SRC_COROSERVER_UMQ_PEER_H_
#include "umq_connection.h"

#include "static_lookup.h"
#include <queue>
#include <coro.h>

#include "strutils.h"
#include <iostream>
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

    using AttachmentData = std::vector<char>;


    using Text = TextBuffer<char, 64>;

    struct Payload {
        Text text;
        std::vector<coro::lazy_future<AttachmentData> > attachments;

        Payload() = default;
        Payload(std::string_view text): text(text) {}
        Payload(std::string_view text, std::vector<coro::lazy_future<AttachmentData> > &&attachments)
            : text(text),attachments(std::move(attachments)) {}
        void add_attachment(AttachmentData attach) {
            attachments.push_back(std::move(attach));
        }
        void add_attachment(coro::lazy_future<AttachmentData> attach) {
            attachments.push_back(std::move(attach));
        }
    };



    using ID = std::uint32_t;

    static constexpr ID version = 1;

    struct Core;

    struct PayloadWithID : Payload {
        ID id;
        PayloadWithID() = default;
        template<typename ... Args>
        PayloadWithID(ID id, Args && ... args):Payload(std::forward<Args>(args)...), id(id) {}

    };

    struct Request : PayloadWithID {
        coro::promise<Payload &&> response;
        Request() = default;
        template<typename ... Args>
        Request(coro::promise<Payload &&> &&prom, Args && ... args)
            :PayloadWithID(std::forward<Args>(args)...)
            ,response(std::move(prom)) {}

    };

    struct SubscriptionCoreDeleter {
        void operator()(Core *x) const;
    };

    class SubscriptionBase {
    public:
        SubscriptionBase() = default;
        SubscriptionBase(ID id, Core *core);
        SubscriptionBase(SubscriptionBase &&x) = default;
        SubscriptionBase(const SubscriptionBase &x) = delete;
        SubscriptionBase &operator=(SubscriptionBase &&x) = default;
        SubscriptionBase &operator=(const SubscriptionBase &x) = delete;
    protected:
        ID _id = 0;
        std::unique_ptr<Core, SubscriptionCoreDeleter> _core;

    };

    ///Contains active subscription
    /**
     * Subscription can be used to listen on subscribed channel
     */
    class Subscription: public SubscriptionBase {
    public:
        using SubscriptionBase::SubscriptionBase;
        ~Subscription();

        ///Receive next data on channel
        /**
         * @return received data
         * @note you need to call this method immediatelly to avoid miss
         * any data. Optimal is to do this in the same thread.
         *
         * (work similar as coro::distributor)
         *
         */
        coro::future<Payload &&> receive();
        ID get_id() const {return _id;}
    };

    class Topic: public SubscriptionBase {
    public:
        using SubscriptionBase::SubscriptionBase;
        ~Topic();
        ///publish to the topic
        /**
         * @param pl payload to publish
         * @return lazy future returns state of the channel. Future is resolved
         * once the message hits the network. The value contain susccess.
         * @retval true published
         * @retval false topic has been closed
         *
         * @note Return value can be asynchronous. However if you don't wait to
         * future resolution, you can test whether it is pending. If the topic
         * was closed, it is always returned as resolved so you can read status
         * false without waiting.
         */
        coro::lazy_future<bool> publish(Payload &pl);
        coro::lazy_future<bool> publish(Payload &&pl);

        coro::future<void> on_unsubscribe();



    };

    Peer();

    State get_state() const;
    ///Connect to other peer
    /**
     * @param conn opened connection
     * @param pl payload sent with the request
     * @return
     */
    coro::future<Payload &&> connect(PConnection &&conn, Payload &&pl = {});

    ///Initialize peer and listen for incoming connection
    /**
     * @param conn opened connection
     * @return connect request. you need to response this request to continue
     */
    coro::future<Request &&> listen(PConnection &&conn);

    ///returns future, which is resolved once the communication is done (disconnected)
    /**
     * @return future object
     *
     * @note you can have only one waiting future, futher calls break previous promises
     */
    coro::future<void> done();


    ///Send request and await for response
    /**
     * @param request request to send
     * @return response
     */
    coro::future<Payload &&> send_request(Payload &request);

    coro::future<Payload &&> send_request(Payload &&request);

    ///Receive request (as server) to generate response
    /**
     * @return received request.
     *
     *
     */
    coro::future<Request &&> receive_request();

    ///Create subscription
    /**
     * @return subscription.
     *
     * You need to create subscription before you can subscribe on a publisher.
     * The publisher need an ID of subscription to correctly label data on
     * the channel. You can use get_id() to retrieve this ID
     */
    Subscription subscribe();


    ///Start publishing
    Topic publish(ID topic_id);


    ///Send message to a channel
    /**
     * Channels allow to send message without additional signaling. Other side
     * must listen on a channel, otherwise the message is lost.
     *
     * @param channel_id channel id
     * @param pl payload
     * @return discardable lazy future
     */
    coro::lazy_future<bool> channel_send(ID channel_id, Payload &pl);
    coro::lazy_future<bool> channel_send(ID channel_id, Payload &&pl);

    ///Receive message on channel
    /**
     * @param channel_id channel id to listen.
     * @return message when received
     *
     * @note Once the message received, you need to call this function again in the
     * same thread, otherwise, the next message can be lost.
     */
    coro::future<Payload &&> channel_receive(ID channel_id);


    static void toBase36(ID id, std::ostream &out);
    static ID fromBase36(std::string_view txt);


protected:

    enum class Type: char {
        ///global errors, empty id
        /**Last message in stream, always leads to closing connection */
        error = 'E',
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
        attachment_error='N',
        ///Hello message Sends by an initiator. ID contains version, payload is accessible by respondent
        hello = 'H',
        ///Welcome message, sends as reply by respondent, ID contains version, payload is response
        welcome = 'W',
        ///Request
        /**Send request. Request must be responded. The ID of response must match to
         * ID of request. There can be only one Response to Request
         */
        request = 'Q',

        ///Response
        /**
         * Response to a request, ID identifies the request
         */
        response = 'P',


        ///Error response to a request
        /**
         * Same meaning as 'P', but indicates error
         * */
        response_error = 'R',

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
        ///Channel message
        channel_message = 'C',

    };




protected:

    struct CoreDeleter {
        void operator()(Core *ptr);
    };
    struct Command;
    struct OpenedRequest;

    using PCore =  std::unique_ptr<Core, CoreDeleter>;

    PCore _core;

};


class ProtocolError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class AttachmentError: public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
    AttachmentError(const std::string_view &txt):runtime_error(std::string(txt)) {}
};
class RequestError: public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
    RequestError(const std::string_view &txt):runtime_error(std::string(txt)) {}
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
