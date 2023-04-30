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
            case State::payload:
                _cur_message.push_back(c ^ _masking[_cur_message.size() & 0x3]);
                _state = _cur_message.size() == _payload_len?State::complete:State::payload;
                break;
            case State::complete:
                _unused_data = data.substr(i);
                return finalize();
        }
    }
    if (_state == State::complete) return finalize();
    return false;
}

void Parser::reset_state() {
    _state = State::first_byte;
    for (int i = 0; i < 4; ++i) _masking[i] = 0;
    _fin = false;
    _masked = false;
    _payload_len = 0;
    _unused_data = {};
}

void Parser::reset() {
    reset_state();
    _cur_message.clear();
}

Message Parser::get_message() const {
    if (_final_type == Type::connClose) {
        std::uint16_t code = 0;
        std::string_view message;
        if (_cur_message.size() >= 2) {
            code = static_cast<unsigned char>(_cur_message[0]) * 256 + static_cast<unsigned char>(_cur_message[1]);
        }
        if (_cur_message.size() > 2) {
            message = std::string_view(_cur_message.data()+2, _cur_message.size() - 3);
        }
        return Message {
            message,
            _final_type,
            code,
            _fin
        };
    } else {
        return Message {
            {_cur_message.data(), _cur_message.size()},
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

    if (!_fin) {
        if (_need_fragmented) {
            auto tmp = _unused_data;
            reset_state();
            return push_data(tmp);
        }
    }
    return true;
}

Builder::Builder(bool client):_client(client) {
    if (_client) {
        std::random_device dev;
        _rnd.seed(dev());
    }
}

bool Builder::operator()(const Message &message, std::vector<char> &data) {
    std::string tmp;
    std::string_view payload = message.payload;

    if (message.type == Type::connClose) {
        tmp.push_back(static_cast<char>(message.code>>8));
        tmp.push_back(static_cast<char>(message.code && 0xFF));
        if (!message.payload.empty()) {
            std::copy(message.payload.begin(), message.payload.end(), std::back_inserter(tmp));
        }
        payload = {tmp.c_str(), tmp.length()+1};
    }

    // opcode and FIN bit
    char opcode = opcodeContFrame;
    bool fin = message.fin;
    if (!_fragmented) {
        switch (message.type) {
            default:
            case Type::unknown: return false;
            case Type::text: opcode = opcodeTextFrame;break;
            case Type::binary: opcode = opcodeBinaryFrame;break;
            case Type::ping: opcode = opcodePing;break;
            case Type::pong: opcode = opcodePong;break;
            case Type::connClose: opcode = opcodeConnClose;break;
        }
    }
    _fragmented = !fin;
    data.push_back((fin << 7) | opcode);
    // payload length
    std::uint64_t len = payload.size();

    char mm = _client?0x80:0;
    if (len < 126) {
        data.push_back(mm| static_cast<char>(len));
    } else if (len < 65536) {
        data.push_back(mm | 126);
        data.push_back(static_cast<char>((len >> 8) & 0xFF));
        data.push_back(static_cast<char>(len & 0xFF));
    } else {
        data.push_back(mm | 127);
        data.push_back(static_cast<char>((len >> 56) & 0xFF));
        data.push_back(static_cast<char>((len >> 48) & 0xFF));
        data.push_back(static_cast<char>((len >> 40) & 0xFF));
        data.push_back(static_cast<char>((len >> 32) & 0xFF));
        data.push_back(static_cast<char>((len >> 24) & 0xFF));
        data.push_back(static_cast<char>((len >> 16) & 0xFF));
        data.push_back(static_cast<char>((len >> 8) & 0xFF));
        data.push_back(static_cast<char>(len & 0xFF));
    }
    char masking_key[4];

    if (_client) {
        std::uniform_int_distribution<> dist(0, 255);

        for (int i = 0; i < 4; ++i) {
            masking_key[i] = dist(_rnd);
            data.push_back(masking_key[i]);
        }
    } else {
        for (int i = 0; i < 4; ++i) {
            masking_key[i] = 0;
        }
    }

    std::transform(payload.begin(), payload.end(), std::back_inserter(data),
      [&, idx = 0](char c) mutable {
        c ^= masking_key[idx];
        idx = (idx + 1) & 0x3;
        return c;
    });
    return true;
}

std::string_view Builder::operator()(const Message &message) {
    _data.clear();
    (*this)(message, _data);
    return {_data.data(), _data.size()};
}

Reader::Reader(Stream s, bool need_fragmented):_s(s), _parser(need_fragmented),_awt(this) {
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
    if (!_builder(msg, _prepared)) return false;
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
