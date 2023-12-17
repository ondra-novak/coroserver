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
    ,_builder(cfg.client) {
        _target.init_as<coro::future<std::string_view>::target_type>([&](auto fut){on_read(fut);});
    }
    ~InternalState() {}

    coro::lazy_future<bool> write(const Message &msg) {
        return _writer.write([&](auto fn){
            _builder(msg, std::forward<decltype(fn)>(fn));
        });
    }

    coro::future<Message> read() {
        if (_closed) return Message{{},Type::connClose, Base::closeNoStatus};
        return [&](auto p) {
            if (_reader.is_complete()) {
                _reader.reset();
            }
            _read_promise = std::move(p);
            auto &f = _fut.as<std::string_view>();
            f << [&]{return _s.read();};
            f.register_target(_target.call([&](auto fut){on_read(fut);}));
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

    coro::lazy_future<bool> close(std::uint16_t code) {
        if (get_state() == State::open) {
            write({{}, Type::connClose, code});
            return _writer.write_eof();
        } else if (_writer) {
            return _writer.write_eof();
        } else {
            return false;
        }
    }

protected:

    void on_read(coro::future<std::string_view> *fut) noexcept { // @suppress("No return")
        try {
            std::string_view data = *fut;
            if (data.empty()) {

                if (_ping_sent) {
                    Message m{"Ping timeout", Type::connClose, Base::closeAbnormal};
                    write(m);
                    _read_promise(m);
                    return;
                } else {
                    _ping_sent = true;
                    write({{}, Type::ping});
                }
            } else {
                _ping_sent = false;
                while (_reader.push_data(data)) {
                    _s.put_back(_reader.get_unused_data());
                    Message m = _reader.get_message();
                    switch (m.type) {
                        case Type::pong: break;
                        case Type::connClose:
                            _closed = true;
                            write({{}, Type::connClose, Base::closeNormal});
                            _read_promise(m);
                            return;
                        case Type::ping:
                            write({m.payload, Type::pong});
                            break;
                        default:
                            _read_promise(m);
                            return;
                    }
                    _reader.reset();
                    data = _s.read_nb();
                }
            }
            auto &f = _fut.as<std::string_view>();
            f << [&]{return _s.read();};
            f.register_target(_target.call([&](auto fut){on_read(fut);}));
        } catch (...) {
            _read_promise.reject();
        }
    }

    _Stream _s;
    Parser _reader;
    MTStreamWriter _writer;
    Builder _builder;
    coro::promise<Message> _read_promise;
    coro::variant_future<std::string_view, void> _fut;
    coro::any_target<> _target;
    bool _ping_sent = false;
    bool _closed = false;

    void destroy();
    friend Stream::Deleter;
};

struct Stream::Deleter {
    void operator()(InternalState *st) const {
        st->destroy();
    };
};


coro::lazy_future<bool> Stream::send(const Message &msg) {
    return _ptr->write(msg);
}


coro::future<Message> Stream::receive() {
    return _ptr->read();
}

Stream::State Stream::get_state() const {
    return _ptr->get_state();
}


std::size_t Stream::get_buffered_size() const {
    return _ptr->get_buffered_size();
}



Stream::Stream(_Stream s, Cfg cfg):_ptr(create(s, cfg)) {}


std::shared_ptr<Stream::InternalState> Stream::create(_Stream &s, Cfg &cfg) {
    return std::shared_ptr<InternalState>(new InternalState(s, cfg), Deleter());
}

coro::lazy_future<bool> Stream::close(std::uint16_t code) {
    return _ptr->close(code);
}

void Stream::InternalState::destroy() {
    auto lzf = close(Base::closeNormal);
    if (!lzf.is_pending()) {
        delete this;
        return;
    }
    auto &f = _fut.as<void>();
    f << [&]{return static_cast<coro::future<void> >(lzf);};
    f.register_target(_target.call([&](auto ){delete this;}));



}

}


}
