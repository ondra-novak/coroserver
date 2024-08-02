#include "websocket_stream.h"
#include "mt_stream.h"

namespace coroserver {

namespace ws{

StreamImpl::StreamImpl(_Stream s, const Cfg &cfg)
    :_s(s),_parser(cfg.need_fragmented),_builder(cfg.client) {}

StreamImpl::~StreamImpl() {
    shutdown();
}

void StreamImpl::shutdown() {
    _s.shutdown();
}


coro::future<Message> StreamImpl::receive() {
    return [&](auto p){
        if (_parser.is_complete()) {
            _parser.reset();
        }
        _rdfut << [this]{return _s.read();};
        _rdfut >> [this,p = std::move(p)]() mutable {parse_msg(std::move(p));};
    };
}

void StreamImpl::parse_msg(coro::promise<Message> rdprom) {
    try {
        std::string_view str = _rdfut;
        if (str.empty()) {
            if (_s.is_read_timeout() && !_pingsent) {
                send({"",Type::ping});
                _pingsent = true;
                _parser.reset();
            } else {
                {
                    std::lock_guard _(_mx);
                    _closed = true;
                }
                rdprom(Message{"",Type::connClose, Base::closeAbnormal});
                return;
            }
        } else {
            _pingsent = false;
            if (_parser.push_data(str)) {
                _s.put_back(_parser.get_unused_data());
                Message msg = _parser.get_message();
                if (msg.type == Type::ping) {
                    send({msg.payload, Type::pong});
                } else if (msg.type == Type::connClose) {
                    send({"",Type::connClose, Base::closeNormal});
                    _closed = true;
                }
                rdprom(_parser.get_message());
                return;
            }
        }
        _rdfut << [this]{return _s.read();};
        _rdfut >> [this, rdprom = std::move(rdprom)]() mutable {
            parse_msg(std::move(rdprom));
        };
    } catch (const coro::await_canceled_exception &) {
        _closed = true;
        rdprom(Message{"",Type::connClose,Base::closeGoingAway});
    } catch (...) {
        rdprom.reject();
    }
}

bool StreamImpl::send(const Message &msg, coro::promise<bool> completion) {
    {
        std::lock_guard _(_mx);
        if (_closed) {
            completion(false);
            return false;
        }
        if (msg.type == Type::connClose) _closed = true;
        _builder(msg, [&](char c){_wrbuff.push_back(c);});
        if (completion) _wrcompl.push_back(std::move(completion));
        if (_pending) return true;
        _pending = true;
        std::swap(_wrbuff, _sendbuff);
        std::swap(_wrcompl, _sendcompl);
    }
    _wrfut << [this]{return _s.write(std::string_view(_sendbuff.data(),_sendbuff.size()));};
    _wrfut >> [this]{complete_write();};
    return true;
}  

void StreamImpl::complete_write() {
    try {
        _sendbuff.clear();
        bool r = _wrfut;
        for (auto &x: _sendcompl) x(true); 
        _sendcompl.clear();
        
        if (r) {
            bool p;
            {
                std::lock_guard _(_mx);
                std::swap(_wrbuff, _sendbuff);
                std::swap(_wrcompl, _sendcompl);
                _pending = !_sendbuff.empty();
                p = _pending;
            }
            if (p) { 
                _wrfut << [this]{return _s.write(std::string_view(_sendbuff.data(),_sendbuff.size()));};
                _wrfut >> [this]{complete_write();};
            } 
        }        
        else {
            {
                std::lock_guard _(_mx);
                _closed = true;
                _pending = false;
            }
            for (auto &x: _wrcompl) x(false);
        }
    } catch (...) {
        auto e = std::current_exception();
        {
            std::lock_guard _(_mx);
            _closed = true;
            _pending = false;
        }
        for (auto &x: _wrcompl) x.reject(e);
    }
}

coro::future<bool> StreamImpl::send_close(unsigned int code) {
    return [&](auto prom) {
        send(Message{"",Type::connClose, static_cast<unsigned short>(code)}, std::move(prom));
    };
}

Stream Stream::create(_Stream s,const Cfg &cfg) {
    return Stream(std::make_shared<StreamImpl>(std::move(s), cfg));
}

}


}
