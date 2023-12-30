#include "umq_stream_connection.h"

namespace coroserver {

namespace umq {

StreamConnection::StreamConnection(Stream stream) :_mtwrite(std::move(stream)) {
    coro::target_simple_activation(_target, [&](auto fut){
        on_data(fut);
    });

}

coro::future<coroserver::umq::Message> StreamConnection::receive() {
    return [&](auto promise) {
        _rdmsg = std::move(promise);
        init_read();
    };
}

unsigned char StreamConnection::calc_bytes(std::uint64_t sz) {
    unsigned char cnt = 0;
    do {
        sz >>=8;
        ++cnt;
    } while (sz);
    return cnt;
}

coro::lazy_future<bool> StreamConnection::send(const coroserver::umq::Message &msg) {
    if (msg._type == MessageType::close) {
        return _mtwrite.write_eof();
    } else {
        auto type = msg._type == MessageType::binary?PkgType::binary:PkgType::text;
        return send(type, msg._payload);
    }


}

coro::lazy_future<bool> StreamConnection::send(PkgType type, const std::string_view &data) {
    return _mtwrite.write([&](auto iter){

        std::uint64_t sz = data.size();
        unsigned char cnt = calc_bytes(sz);
        char hdr = static_cast<char>(type) | (cnt-1);

        *iter = hdr;
        ++iter;
        for (unsigned char i = cnt; i > 0; ) {
            --i;
            *iter = static_cast<char>((sz >> (8*i)) & 0xFF);
            ++iter;
        }
        std::copy(data.begin(), data.end(), iter);
    });

}


unsigned int StreamConnection::get_hdr_size(unsigned char hdr_first_byte) {
    return (hdr_first_byte & 0x7) + 2;
}


std::uint64_t StreamConnection::get_pkg_total_size() const {
    std::size_t needsz = get_hdr_size(_buffer[0]);
    std::size_t x =0;
    for(std::size_t i = 1; i < needsz; ++i) {
        x = (x << 8) | _buffer[i];
    }
    return x + needsz;
}

void StreamConnection::on_data(coro::future<std::string_view> *fut) {
    try {
        auto s = _mtwrite.getStreamDevice();

        std::string_view data = fut->get();
        if (data.empty()) {
            if (s->is_read_timeout() && !_pinged) {
                send(PkgType::ping, {});
                _pinged = true;
            } else {
                _rdmsg(Message{MessageType::close,{}});
                return;
            }
        } else {
            _pinged = false;
            while (!data.empty()) {
                switch (_phase) {
                    default:
                    case Phase::header: {
                            _buffer.push_back(data[0]);
                            _phase = Phase::size;
                            data = data.substr(1);
                        } break;
                    case Phase::size: {
                            std::size_t hdrsz = get_hdr_size(_buffer[0]);
                            auto sub = data.substr(0, hdrsz - _buffer.size());
                            data = data.substr(sub.size());
                            _buffer.insert(_buffer.end(), sub.begin(), sub.end());
                            if (_buffer.size() == hdrsz) {
                                if (get_pkg_total_size() == hdrsz) {
                                    s->put_back(data);
                                    process_pkg();
                                    return;
                                }
                                _phase = Phase::payload;
                            }
                        }
                        break;
                    case Phase::payload: {
                            std::size_t tot = get_pkg_total_size();
                            auto sub = data.substr(0, tot - _buffer.size());
                            data = data.substr(sub.size());
                            _buffer.insert(_buffer.end(), sub.begin(), sub.end());
                            if (_buffer.size() == tot) {
                                s->put_back(data);
                                process_pkg();
                                return;
                            }
                        }
                        break;
                }
            }
        }

        _fut << [&]{return s->read();};
        _fut.register_target(_target);
    } catch (...) {
        _rdmsg.reject();
    }
}

void StreamConnection::process_pkg() {
    PkgType type = static_cast<PkgType>(_buffer[0] & ~0x7);
    auto hdrsize = get_hdr_size(_buffer[0]);
    std::string_view payload(_buffer.data()+hdrsize, _buffer.size()-hdrsize);
    switch (type) {
        case PkgType::text:
            _rdmsg(Message{MessageType::text, payload});
            return;
        case PkgType::binary:
            _rdmsg(Message{MessageType::binary, payload});
            return;
        case PkgType::ping:
            send(PkgType::pong, {});
            return;
        default:
        case PkgType::pong:
            init_read();
            return;
    }
}

void StreamConnection::init_read() {
    _buffer.clear();
    _phase = Phase::header;
    _fut << [&]{return _mtwrite.getStreamDevice()->read();};
    _fut.register_target(_target);
}


}
}

