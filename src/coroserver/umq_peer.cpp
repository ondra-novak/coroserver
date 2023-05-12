#include "umq_peer.h"
#include "umq_ws_connection.h"
#include <sstream>

namespace coroserver {

namespace umq {

Peer::Peer()
        :_close_promise(_close_future.get_promise())
        ,_on_msg_awt(this)
        ,_att_ready(this)
{}

void Peer::start_reader() {

    _on_msg_awt << [&]{return _conn->receive();};
}

Peer::State Peer::get_state() const {
    if (_conn == nullptr) return State::unbound;
    if (_closed) return State::closed;
    if (_closing.test()) return State::closing;
    return State::open;
}

cocls::suspend_point<void> Peer::on_message(cocls::future<Message> &f) noexcept {
    try {
        bool st = true;
        const Message &msg = *f;
        switch (msg._type) {
            default:
            case MessageType::close:
                st = false;
                break;
            case MessageType::text:
                st = recvTextMessage(msg._payload);
                break;
            case MessageType::binary:
                st = recvBinaryMessage(msg._payload);
                break;
        }
        if (!st) {
            _closed = true;
            _conn->send({MessageType::close,{}});
            cocls::suspend_point<void> a = _close_promise();
            a << _hello_request(cocls::drop);
            a << _welcome_response(cocls::drop);
            return a;
        }
        _on_msg_awt << [&]{return _conn->receive();};
        return {};

    } catch (...) {
        try {
            _closed = true;
            _conn->send({MessageType::close,{}});
        } catch (...) {
            //empty
        }
        cocls::suspend_point<void> a = _close_promise(std::current_exception());
        a << _hello_request(std::current_exception());
        a << _welcome_response(std::current_exception());
        return a;
    }

}


void Peer::allocate_attachments(std::string_view txtcount, std::vector<cocls::shared_future<std::vector<char> > > &attach) {
    auto cnt = string2unsigned<unsigned int>(txtcount.begin(), txtcount.end(), 10);
    for (unsigned int i = 0; i < cnt; ++i) {
        attach.push_back(
                cocls::shared_future<std::vector<char> >([&](auto promise) {
            _awaited_binary.push(std::move(promise));
        }));
    }
}

bool Peer::recvTextMessage(std::string_view message) {
    std::vector<cocls::shared_future<std::vector<char> > > attachments;
    try {
        do {
            if (message.empty()) {
                close(ErrorCode::invalid_message);
                return false;
            }
            Type mt = static_cast<Type>(message[0]);
            auto pos = message.find('\n',1);
            std::string_view id;
            std::string_view payload;
            if (pos == message.npos) id = message;
            else {
                id = message.substr(1,pos-1);
                payload = message.substr(pos+1);
            }
            switch (mt) {
                default: throw makeError(ErrorCode::invalid_message);
                case Type::attachment_count: allocate_attachments(id, attachments);
                                             message = payload;
                                             break;
                case Type::attechment_error: on_attachment_error(UMQException(payload));
                                             return true;
                case Type::hello: on_hello_message(id, {message, std::move(attachments)});
                                  return true;
                case Type::welcome: on_welcome_message(id, {message, std::move(attachments)});
                                  return true;

            }
        }
        while (true);
    } catch (const UMQException &e) {
        close(e);
        return false;
    }

}

void Peer::close() {
    if (_closing.test_and_set(std::memory_order_relaxed) == false) {
        _conn->send({MessageType::close});
    }
}

void Peer::close(const UMQException &error) {
    if (_closing.test_and_set(std::memory_order_relaxed) == false) {
        send(Type::error, "", error.get_serialized());
        _conn->send({MessageType::close});
    }
}

bool Peer::send(Type type, std::string_view id, std::string_view payload, int attachments) {
    std::ostringstream str;
    if (attachments) {
        str << static_cast<char>(Type::attachment_count) << attachments << '\n';
    }
    str << static_cast<char>(type) << id << '\r' << payload;
    return _conn->send({MessageType::text, str.view()});
}

bool Peer::send(Type type, std::string_view id, const Payload &pl) {
    if (pl.attachments.empty()) {
        return send(type, id, pl.text, 0);
    } else {
        bool need_start;
        {
            std::lock_guard  lk(_attach_send_queue_mx);
            need_start = _attach_send_queue.empty();
            if (!send(type, id, pl.text, pl.attachments.size())) return false;
            for (const auto &c : pl.attachments) {
                _attach_send_queue.push(c);
            }
        }
        if (need_start) {
            _att_ready_peer_ref = shared_from_this();
            on_attachment_ready(&_att_ready);
        }
        return true;
    }
}

cocls::suspend_point<void> Peer::on_attachment_ready(cocls::awaiter *awt) noexcept {
    std::unique_lock  lk(_attach_send_queue_mx);
    while (!_attach_send_queue.empty() && !_closing.test()) {
        auto &f = _attach_send_queue.front();
        if (!f.subscribe(awt)) {
            try {
                const auto &data = f.value();
                if (!_conn->send(Message{MessageType::binary, {data.data(), data.size()}})) {
                    break;
                }
            } catch (UMQException &e) {
                if (!send(Type::attechment_error, {}, e.get_serialized())) {
                    break;
                }
            } catch (const std::exception &e) {
                UMQException err(static_cast<int>(ErrorCode::internal_error), e.what());
                if (!send(Type::attechment_error, {}, err.get_serialized())) {
                    break;
                }
            } catch (...) {
                UMQException err(makeError(ErrorCode::internal_error));
                if (!send(Type::attechment_error, {}, err.get_serialized())) {
                    break;
                }
            }
            _attach_send_queue.pop();
        } else {
            return {};
        }
    }
    _att_ready_peer_ref.reset();
    return {};
}

void Peer::close(ErrorCode code) {
    return close(makeError(code));
}

bool Peer::recvBinaryMessage(std::string_view message) {
    if (_awaited_binary.empty()) {
        close(ErrorCode::unexpected_attachment);
        return false;
    }
    auto prom = std::move(_awaited_binary.front());
    prom(message.begin(), message.end());
    return true;
}

UMQException::UMQException(int code, std::string_view message):_code(code) {
    _msgtext = std::to_string(_code).append(" ").append(message);
    _message = std::string_view(_msgtext).substr(_msgtext.size()-message.size());
}

UMQException::UMQException(std::string_view txtmsg):_msgtext(txtmsg) {
    char *endptr;
    int code = strtol(_msgtext.c_str(), &endptr, 10);
    while (*endptr && std::isspace(*endptr)) ++endptr;
    _code = code;
    _message = std::string_view(endptr);
}

std::string_view UMQException::get_serialized() const {
    return _msgtext;
}

UMQException Peer::makeError(ErrorCode code) {
    return UMQException(static_cast<int>(code),strErrorCode[code]);
}

void Peer::on_attachment_error(const UMQException &e) {
    if (_awaited_binary.empty()) {
        throw makeError(ErrorCode::unexpected_attachment);
    }
    auto prom = std::move(_awaited_binary.front());
    prom(std::make_exception_ptr(e));
}

void Peer::on_hello_message(const std::string_view &version, const Payload &payload) {
    if (version == strVersion) throw makeError(ErrorCode::unsupported_version);
    bool b = _hello_request(HelloMessage{payload.text,
        payload.attachments,
        cocls::make_promise<Payload>([me = shared_from_this()](cocls::future<Payload> &f){
            try {
                const Payload &p = *f;
                me->send(Type::welcome, {}, p);
            } catch (UMQException &e) {
                me->close(e);
            } catch (std::exception &e) {
                me->close(UMQException(static_cast<int>(ErrorCode::internal_error), e.what()));
            } catch (...) {
                me->close(ErrorCode::internal_error);
            }
    })});
    if (!b) throw makeError(ErrorCode::invalid_message);
}

void Peer::on_welcome_message(const std::string_view &version, const Payload &payload) {
    if (version == strVersion) throw makeError(ErrorCode::unsupported_version);
    bool b = _welcome_response(payload);
    if (!b) throw makeError(ErrorCode::invalid_message);
}



cocls::future<Peer::HelloMessage> Peer::start_server(PConnection conn) {
    return [&](auto promise) {
        _hello_request = std::move(promise);
        _conn = std::move(conn);
        start_reader();
    };
}

cocls::future<Peer::Payload> Peer::start_client(PConnection conn, Payload hello) {
    return [&](auto promise) {
        _welcome_response = std::move(promise);
        _conn = std::move(conn);
        start_reader();
        send(Type::hello, {}, hello);
    };


}

void Peer::HelloMessage::accept() {
    accept(std::string_view{});
}

void Peer::HelloMessage::accept(std::string_view payload) {
    accept(Payload{payload});
}

void Peer::HelloMessage::accept(Payload payload) {
    _response(payload);
}


}

}

