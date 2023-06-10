#include "websocket_stream.h"
#include "mt_stream.h"

namespace coroserver {

namespace ws{

class Stream::InternalState {
public:
    InternalState(_Stream &s, Cfg &cfg)
    :_s(s)
    ,_reader(cfg.need_fragmented)
    ,_writer(s)
    ,_builder(cfg.client)
    ,_awt(*this)
    ,_awt_destroy(*this) {}

    cocls::suspend_point<bool> write(const Message &msg) {
        return _writer([&](auto fn){
            _builder(msg, std::forward<decltype(fn)>(fn));
        });
    }

    cocls::future<Message> read() {
        if (_closed) return cocls::future<Message>::set_value(Message{{},Type::connClose, Base::closeNoStatus});
        return [&](auto p) {
            if (_reader.is_complete()) {
                _reader.reset();
            }
            _read_promise = std::move(p);
            _awt << [&]{return _s.read();};
        };
    }

    std::size_t get_buffered_size() const {
        return _writer.get_buffered_size();
    }

    State get_state() const {
        bool wr_open = _writer;
        bool rd_open = !_closed;
        if (wr_open && rd_open) return State::open;
        if (!wr_open && !rd_open) return State::closed;
        return State::closing;
    }

    cocls::suspend_point<bool> close(std::uint16_t code) {
        return _writer([&](auto fn){
            _builder({{}, Type::connClose, code}, std::forward<decltype(fn)>(fn));
            _writer.close();
        });
    }

    void destroy() {
        //write close message
        close(Base::closeNormal);
        //sync to idle, then destroy
        _awt_destroy << [&]{return _writer.wait_for_idle();};
    }

    cocls::future<void> wait_for_flush() {
        return _writer.wait_for_flush();
    }

    cocls::future<void> wait_for_idle() {
        return _writer.wait_for_idle();
    }


protected:

    cocls::suspend_point<void> on_read(cocls::future<std::string_view> &fut) noexcept { // @suppress("No return")
        try {
            std::string_view data = *fut;
            if (data.empty()) {
                if (_ping_sent || !_s.is_read_timeout()) {
                    close(Base::closeAbnormal);
                    _closed = true;
                    return _read_promise(Message{{},Type::connClose, Base::closeAbnormal});
                } else {
                    _ping_sent = true;
                    write({{}, Type::ping});
                    _awt << [&]{return _s.read();};
                    return {};
                }
            }
            _ping_sent = false;
            while (_reader.push_data(data)) {
                _s.put_back(_reader.get_unused_data());
                Message m = _reader.get_message();
                switch (m.type) {
                    case Type::pong: break;
                    case Type::connClose:
                        close(Base::closeNormal);
                        return _read_promise(m);
                    case Type::ping:
                        write({m.payload, Type::pong});
                        break;
                    default:
                        return _read_promise(m);
                }
                _reader.reset();
                data = _s.read_nb();
            }
            _awt << [&]{return _s.read();};
            return {};
        } catch (...) {
            return _read_promise(std::current_exception());
        }
    }

    cocls::suspend_point<void> destroy_self(cocls::future<void> &) noexcept {
        //now we know, that writer is idle
        //we can destroy object
        delete this;
        //no suspend point
        return {};
    }


    _Stream _s;
    Parser _reader;
    MTStreamWriter _writer;
    Builder _builder;
    cocls::promise<Message> _read_promise;
    cocls::call_fn_future_awaiter<&InternalState::on_read> _awt;
    cocls::call_fn_future_awaiter<&InternalState::destroy_self> _awt_destroy;
    bool _ping_sent = false;
    bool _closed = false;
};

struct Stream::Deleter {
    void operator()(InternalState *st) const {
        st->destroy();
    };
};


cocls::suspend_point<bool> Stream::write(const Message &msg) {
    return _ptr->write(msg);
}

cocls::suspend_point<bool> Stream::close(std::uint16_t code) {
    return _ptr->close(code);
}

cocls::suspend_point<bool> Stream::close() {
    return _ptr->close(Base::closeNormal);
}

cocls::future<Message> Stream::read() {
    return _ptr->read();
}

Stream::State Stream::get_state() const {
    return _ptr->get_state();
}


std::size_t Stream::get_buffered_size() const {
    return _ptr->get_buffered_size();
}



Stream::Stream(_Stream s, Cfg cfg):_ptr(create(s, cfg)) {}

cocls::future<void> Stream::wait_for_flush() {
    return _ptr->wait_for_flush();
}

cocls::future<void> Stream::wait_for_idle() {
    return _ptr->wait_for_idle();
}

std::shared_ptr<Stream::InternalState> Stream::create(_Stream &s, Cfg &cfg) {
    return std::shared_ptr<InternalState>(new InternalState(s, cfg), Deleter());
}

}

}
