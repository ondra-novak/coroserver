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
        _target.on_activate<coro::future<std::string_view>::target_type>([&](auto fut){on_read(fut);});
    }
    ~InternalState() {}

    void write(const Message &msg) {
        _writer([&](auto fn){
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
            new(&_read_fut) auto(_s.read());
            _read_fut.register_target(_target.as<coro::future<std::string_view>::target_type>());
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

    coro::future<void> wait_for_flush() {
        return _writer.wait_for_flush();
    }

    coro::future<void> wait_for_idle() {
        return _writer.wait_for_idle();
    }

protected:

    void on_read(coro::future<std::string_view> *fut) noexcept { // @suppress("No return")
        try {
            std::string_view data = *fut;
            std::destroy_at(fut);
            if (data.empty()) {

                if (_ping_sent) {
                    Message m{"Ping timeout", Type::connClose, Base::closeAbnormal};
                    write(m);
                    _read_promise(m);
                } else {
                    _ping_sent = true;
                    write({{}, Type::ping});
                    new (&_read_fut) auto(_s.read());
                    _read_fut.register_target(_target.as<coro::future<std::string_view>::target_type>());
                }
                return;
            }
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
            new(&_read_fut) auto(_s.read());
            _read_fut.register_target(_target.as<coro::future<std::string_view>::target_type>());
        } catch (...) {
            std::destroy_at(&_read_fut);
            _read_promise.reject();
        }
    }

    _Stream _s;
    Parser _reader;
    MTStreamWriter _writer;
    Builder _builder;
    coro::promise<Message> _read_promise;
    union {
        coro::future<std::string_view> _read_fut;
        coro::future<void> _on_idle_fut;
    };
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


void Stream::write(const Message &msg) {
    return _ptr->write(msg);
}


coro::future<Message> Stream::read() {
    return _ptr->read();
}

Stream::State Stream::get_state() const {
    return _ptr->get_state();
}


std::size_t Stream::get_buffered_size() const {
    return _ptr->get_buffered_size();
}



Stream::Stream(_Stream s, Cfg cfg):_ptr(create(s, cfg)) {}

coro::future<void> Stream::wait_for_flush() {
    return _ptr->wait_for_flush();
}

coro::future<void> Stream::wait_for_idle() {
    return _ptr->wait_for_idle();
}

std::shared_ptr<Stream::InternalState> Stream::create(_Stream &s, Cfg &cfg) {
    return std::shared_ptr<InternalState>(new InternalState(s, cfg), Deleter());
}

void Stream::InternalState::destroy() {
    new(&_on_idle_fut) auto(_writer.wait_for_idle());
    _on_idle_fut.register_target(_target.on_activate<coro::future<void>::target_type>(
            [&](auto fut){
        std::destroy_at(fut);
        delete this;
    }));



}

}


}
