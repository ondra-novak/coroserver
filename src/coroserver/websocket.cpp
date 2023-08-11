#include "websocket.h"

#include <random>


namespace coroserver {

namespace ws {

bool Parser::push_data(std::string_view data) {
    std::size_t sz = data.size();
    for (std::size_t i = 0; i < sz; i++) {
        char c = data[i];
        switch (_state) {
            case State::first_byte:
                _fin = (c & 0x80) != 0;
                _type = c & 0xF;
                _state = State::payload_len;
                break;
            case State::payload_len:
                _masked = (c & 0x80) != 0;
                c &= 0x7F;
                if (c == 127) _state = State::payload_len7;
                else if (c == 126) _state = State::payload_len1;
                else {
                    _payload_len = c;
                    _state = State::masking;
                }
                break;
            case State::payload_len0:
            case State::payload_len1:
            case State::payload_len2:
            case State::payload_len3:
            case State::payload_len4:
            case State::payload_len5:
            case State::payload_len6:
            case State::payload_len7:
                _payload_len = (_payload_len<<8) + static_cast<unsigned char>(c);
                _state = static_cast<State>(static_cast<std::underlying_type_t<State> >(_state)+1);
                break;
            case State::masking:
                _state = _masked?State::masking1:State::payload;
                --i; //retry this byte
                break;
            case State::masking1:
            case State::masking2:
            case State::masking3:
            case State::masking4:
                _masking[static_cast<int>(_state) - static_cast<int>(State::masking1)] = c;
                _state = static_cast<State>(static_cast<std::underlying_type_t<State> >(_state)+1);
                break;
            case State::payload_begin:
                _state = _payload_len?State::payload:State::complete;
                --i;
                break;
            case State::payload:
                if (_cur_readbytes < _max_message_size_current) {
                    _cur_message.push_back(c ^ _masking[_cur_readbytes & 0x3]);
                }
                ++_cur_readbytes;
                _state = _cur_readbytes == _payload_len?State::complete:State::payload;
                break;
            case State::complete:
                _unused_data = data.substr(i);
                return finalize();
        }
    }
    if (_state >= State::payload_begin &&  _cur_readbytes == _payload_len) {
        return finalize();
    }
    return false;
}

void Parser::reset_state() {
    _state = State::first_byte;
    for (int i = 0; i < 4; ++i) _masking[i] = 0;
    _fin = false;
    _masked = false;
    _payload_len = 0;
    _unused_data = {};
    _cur_message.clear();
    _cur_readbytes = 0;
}

void Parser::reset() {
    _final_message.clear();
    if (_type == opcodeContFrame) {
        _final_message.shrink_to_fit();
    }
    reset_state();
}

Message Parser::get_message() const {
    if (_final_type == Type::connClose) {
        std::uint16_t code = 0;
        std::string_view message;
        if (_final_message.size() >= 2) {
            code = static_cast<unsigned char>(_final_message[0]) * 256 + static_cast<unsigned char>(_final_message[1]);
        }
        if (_final_message.size() > 2) {
            message = std::string_view(_final_message.data()+2, _final_message.size() - 3);
        }
        return Message {
            message,
            _final_type,
            code,
            _fin
        };
    } else {
        return Message {
            {_final_message.data(), _final_message.size()},
            _final_type,
            _type,
            _fin
        };
    }
}



bool Parser::finalize() {
    switch (_type) {
        case opcodeContFrame: break;
        case opcodeConnClose: _final_type = Type::connClose; break;
        case opcodeBinaryFrame:  _final_type = Type::binary;break;
        case opcodeTextFrame:  _final_type = Type::text;break;
        case opcodePing:  _final_type = Type::ping;break;
        case opcodePong:  _final_type = Type::pong;break;
        default: _final_type = Type::unknown;break;
    }

    if (_cur_readbytes > _cur_message.size()) {
        _final_type = Type::largeFrame;
    }

    if (_final_message.empty()) {
        std::swap(_final_message, _cur_message);
    } else {
        std::copy(_cur_message.begin(), _cur_message.end(), std::back_inserter(_final_message));
        _cur_message.clear();
    }

    if (!_fin) {
        if (!_need_fragmented) {
            auto tmp = _unused_data;
            reset_state();
            _max_message_size_current = _max_message_size - _final_message.size();
            return push_data(tmp);
        }
    }
    _max_message_size_current = _max_message_size;
    _cur_readbytes = 0;

    return true;
}


Reader::Reader(Stream s,  std::size_t max_message_size, bool need_fragmented):_s(s), _parser(max_message_size, need_fragmented),_awt(this) {
}

cocls::future<Message &> Reader::operator ()() {
    if (_parser.is_complete()) _parser.reset();
    return _awt << [&]{return _s.read();};
}

cocls::suspend_point<void> Reader::read_next(std::string_view &data,  cocls::promise<Message &> &prom) {

    if (data.empty()) return prom(cocls::drop);

    bool r = _parser.push_data(data);
    if (r) {
        _s.put_back(_parser.get_unused_data());
        msg = _parser.get_message();
        return prom(msg);
    } else {
        _awt(std::move(prom)) << [&]{return _s.read();};
        return {};
    }
}

Writer::Writer(Stream s, bool client):_s(s), _builder(client),_awt(*this) {
}

cocls::suspend_point<bool> Writer::operator()(const Message &msg) {
    std::unique_lock lk(_mx);
    if (_closed) return false;
    return do_write(msg, lk);
}

cocls::suspend_point<bool> Writer::operator()(const Message &msg, cocls::promise<void> sync) {
    std::unique_lock lk(_mx);
    if (_closed) {
        lk.unlock();
        return {sync(), false};
    }
    _waiting.push_back(std::move(sync));
    return do_write(msg, lk);
}

cocls::suspend_point<void> Writer::flush(std::unique_lock<std::mutex> &lk) {
    cocls::suspend_point<void> out;;
    for (auto &p: _waiting) out << p();
    _waiting.clear();
    _pending = true;
    std::swap(_prepared, _pending_write);
    lk.unlock();
    _awt << [&]{return _s.write({_pending_write.data(), _pending_write.size()});};
    return out;
}

cocls::suspend_point<bool> Writer::do_write(const Message &msg, std::unique_lock<std::mutex> &lk) {
    if (!_builder(msg, [&](char c){_prepared.push_back(c);})) return false;
    if (msg.type == Type::connClose) _closed = true;
    return {_pending?cocls::suspend_point<void>():flush(lk), true};
}

cocls::future<void> Writer::sync_for_idle() {
    std::unique_lock lk(_mx);
    return [&](cocls::promise<void> p) {
        if (!_pending) {
            p();
        } else {
            _cleanup = std::move(p);
        }
    };
}

cocls::suspend_point<void> Writer::finish_write(cocls::future<bool> &val) noexcept {
    std::unique_lock lk(_mx);
    try {
        bool ret = *val;
        _pending_write.clear();
        if (ret) {
            if (!_prepared.empty()) {
                return flush(lk);
            } else {
                _pending = false;
                return _cleanup();
            }
        }
    } catch (...) {
        //empty
    }
    _closed = true;
    _pending = false;
    _prepared.clear();
    return _cleanup();

}

}

}

#include "websocket_stream.h"
