#pragma once
#ifndef SRC_COROSERVER_UMQ_CONNECTION_H_
#define SRC_COROSERVER_UMQ_CONNECTION_H_
#include <string_view>
#include <cocls/future.h>

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
    virtual cocls::future<Message> receive() = 0;
    virtual bool send(const Message &msg) = 0;
    virtual ~IConnection() = default;
};


using PConnection = std::unique_ptr<IConnection>;

}

}




#endif /* SRC_COROSERVER_UMQ_CONNECTION_H_ */
