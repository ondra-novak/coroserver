#pragma once
#ifndef SRC_COROSERVER_UMQ_WS_CONNECTION_H_
#define SRC_COROSERVER_UMQ_WS_CONNECTION_H_
#include "websocket_stream.h"

#include "umq_connection.h"

#include <memory>
namespace coroserver {

namespace umq {

class WsConnection: public IConnection  {
public:
    WsConnection(ws::Stream s):_s(std::move(s)),_awt(*this) {}

    virtual cocls::future<Message> receive() override;
    virtual bool send(const Message &msg) override;

    static PConnection create(ws::Stream s) {
        return std::make_unique<WsConnection>(std::move(s));
    }

protected:
    cocls::suspend_point<void> on_receive(cocls::future<ws::Message> &f) noexcept;
    ws::Stream _s;
    cocls::call_fn_future_awaiter<&WsConnection::on_receive> _awt;
    cocls::promise<Message> _result;
};

inline cocls::future<Message> WsConnection::receive() {
    return [&](auto promise) {
        _result = std::move(promise);
        _awt << [&]{return _s.read();};
    };
}

inline bool WsConnection::send(const Message &msg) {
    switch (msg._type) {
        case MessageType::close: return _s.close();
        case MessageType::binary: return _s.write({msg._payload, ws::Type::binary});
        case MessageType::text: return _s.write({msg._payload, ws::Type::text});
    }
    return true;
}


inline cocls::suspend_point<void> WsConnection::on_receive(cocls::future<ws::Message> &f) noexcept {
    try {
        const ws::Message &msg = *f;
        switch (msg.type) {
            case ws::Type::connClose: return _result(Message{MessageType::close, {}});
            case ws::Type::text: return _result(Message{MessageType::text, msg.payload});
            case ws::Type::binary: return _result(Message{MessageType::binary, msg.payload});
            default:break;
        }
        _awt << [&]{return _s.read();};
        return {};
    } catch (...) {
        return _result(std::current_exception());
    }
}



}

}

#endif /* SRC_COROSERVER_UMQ_WS_CONNECTION_H_ */
