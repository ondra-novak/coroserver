#pragma once
#ifndef SRC_COROSERVER_UMQ_CONNECTION_H_
#define SRC_COROSERVER_UMQ_CONNECTION_H_
#include <string_view>
#include <coro.h>

namespace coroserver {


namespace umq {

enum class MessageType {
    ///message is in text format (utf-8)
    text,
    ///message is in binary format (utf-8)
    binary,
    ///stream has been closed (EOF message)
    close,
};

struct Message {
    MessageType _type;
    std::string_view _payload = {};

};


class IConnection {
public:
    ///receive message (async)
    virtual coro::future<Message> receive() = 0;
    ///send message
    /**
     * @param msg message to send. You can semd close frame to close the stream
     * @return optional future, which can be awaited to delay code until the message
     * hits the network.
     */
    virtual coro::lazy_future<bool> send(const Message &msg) = 0;
    ///destructor
    virtual ~IConnection() = default;
};


using PConnection = std::unique_ptr<IConnection>;

}

}




#endif /* SRC_COROSERVER_UMQ_CONNECTION_H_ */
