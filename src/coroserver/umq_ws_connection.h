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
    WsConnection(ws::Stream s):_s(std::move(s)) {
        coro::target_member_fn_activation<&WsConnection::on_receive>(_target, this);
    }

    virtual coro::future<Message> receive() override {
        return [&](auto promise) {
            _result = std::move(promise);
            _fut << [&]{return _s.receive();};
            _fut.register_target(_target);
        };
    }

    virtual coro::lazy_future<bool> send(const Message &msg) override {
        switch (msg._type) {
            case MessageType::close: return _s.close();
            case MessageType::binary: return _s.send({msg._payload, ws::Type::binary});
            case MessageType::text: return _s.send({msg._payload, ws::Type::text});
        }
    }

    static PConnection create(ws::Stream s) {
        return std::make_unique<WsConnection>(std::move(s));
    }

protected:
    ws::Stream _s;
    coro::promise<Message> _result;
    coro::future<ws::Message> _fut;
    coro::future<ws::Message>::target_type _target;

    void on_receive(coro::future<ws::Message> *fut) noexcept {
        try {
            const ws::Message &msg = *fut;
            switch (msg.type) {
                case ws::Type::connClose:
                    _result(Message{MessageType::close, {}});
                    return;
                case ws::Type::text:
                    _result(Message{MessageType::text, msg.payload});
                    return;
                case ws::Type::binary:
                    _result(Message{MessageType::binary, msg.payload});
                    return;
                default:break;
            }
            _fut << [&]{return _s.receive();};
            _fut.register_target(_target);
        } catch (...) {
            _result.reject();
        }
    }
};




}

}

#endif /* SRC_COROSERVER_UMQ_WS_CONNECTION_H_ */
