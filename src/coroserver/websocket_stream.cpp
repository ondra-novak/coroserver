#include "websocket_stream.hpp"


namespace coroserver {
namespace ws {

WebSocketStream::WebSocketStream(Stream s, bool server, bool need_fragmented)
    :WebSocketStream(BufferedStream(std::move(s)), server, need_fragmented) {}
WebSocketStream::WebSocketStream(BufferedStream s, bool server, bool need_fragmented)
: _stream(std::move(s))
,_parser(_buffer, need_fragmented)
,_server(server) {
if (_server){
    std::random_device seeddev;
    _rand_gen = std::mt19937(seeddev());
}

}


WebSocketStream::~WebSocketStream() {
    close();
}

awaitable<Message> WebSocketStream::read() {
    _parser.reset();
    auto awt = _stream.read();
    while (awt.await_ready()) {
        std::string_view data = awt.await_resume();
        if (data.empty()) return Message{"",Type::connClose, Base::closeAbnormal};
        if (_parser.push_data(data)) {
            auto msg = _parser.get_message();
            if (handle_message(msg)) return read(); 
            return msg;
        }
        awt = _stream.read();
    }
    _read_callback.set_awaiter(awt);
    return [this](awaitable<Message>::result result) {
        if (!result) {
            _read_callback.get_awaiter().cancel();
            return result.set_empty();
        }
        return _read_callback.await([this,r = std::move(result)](auto &awt) mutable {
            try {
                if (awt.has_value()) {
                    std::string_view data = awt.await_resume();
                    if (data.empty()) return r(Message{"",Type::connClose, Base::closeAbnormal});
                    _ping_sent = false;
                    if (_parser.push_data(data)) {
                        auto msg = _parser.get_message();
                        if (handle_message(msg)) {
                            _parser.reset();
                            return _read_callback.await_cont(_stream.read());
                        }
                        return r(msg);
                    } else {
                        return _read_callback.await_cont(_stream.read());
                    }
                } else {
                    if (!_ping_sent) {
                        _ping_sent = true;
                        write({{}, Type::ping});
                        return _read_callback.await_cont(_stream.read());
                    } else {
                        return r(Message{"",Type::connClose, Base::closeAbnormal});
                    }
                }                        
            } catch (...) {
                return r.set_exception(std::current_exception());
            }
        });                
    };
}

std::array<std::uint8_t, 4> WebSocketStream::generate_masking_key() {
    std::array<std::uint8_t, 4> key;
    std::uniform_int_distribution<std::uint8_t> dist(0, 255);
    for (auto &k : key) {
        k = dist(_rand_gen);
    }
    return key;
}
bool WebSocketStream::write(const Message &message)
{    
    bool c = true;
    bool r = _stream.write([&](auto output_iter){
        //under lock!
        if (_close_sent) {
            c = false;
            return;
        }
        if (message.type == Type::connClose) _close_sent = true;     
        build(message, [&](char c) {
            *output_iter = c;
            ++output_iter;
        }, _server ? nullptr : generate_masking_key().data());    
    });
    return r && c;
}

bool WebSocketStream::close(std::uint16_t code, std::string_view message)
{
    if (get_state() == StreamState::closing) return true;
    return write({message, Type::connClose, code});
}

bool WebSocketStream::handle_message(const Message &msg)
{
    switch (msg.type) {    
        default: return false;
        case Type::connClose:
            if (get_state() == StreamState::closing) {
                _stream.shutdown(); //shutdown the stream, no longer usable
                return true;    //we arleady closing, ignore this message
            } else {
                _close_recv = true;
                _stream.close();//response that we are closing
                return false;   //also pass the message to the client
            }
        case Type::ping:
            write({msg.payload, Type::pong});
            return true; //handled ping, no need to pass to the client
        case Type::pong:
            return true; //handled pong, no need to pass to the client        
    }
}

}
}
