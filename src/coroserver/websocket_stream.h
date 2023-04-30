#ifndef SRC_COROSERVER_WEBSOCKET_STREAM_H_
#define SRC_COROSERVER_WEBSOCKET_STREAM_H_

#include "stream.h"
#include "websocket.h"
#include <cocls/mutex.h>
#include <cocls/generator.h>

namespace coroserver {

namespace ws {

using _Stream = Stream;


///A websocket stream
/**
 * Websocket stream can be created from connected stream. Initial handshake is not
 * handled by this object.
 *
 * Websocket stream is object, which can be read for messages, or messages can be written.
 *
 * Reading is not MT Safe, there could be just one reader, which receives messages.
 *
 * Writing is MT safe, it is possible to write messages without waiting on completion.
 * The stream contains a buffer, which is filled with messages ready to send while
 * network transfer is slow. As the buffer is unable to discard any mesage, you
 * need to take care of speed of filling the buffer. To slow down the write
 * speed and synchronize with the network, you can tie a promise with each message,
 * which is resolved once the message is written to the network.
 *
 * The object is shared by copying. Stream can be implicitly closed, when
 * by releasing all references (in this case, close message is automatically
 * send as the last message in the stream)
 */
class Stream {
public:
    struct Cfg {
        bool client = false;
        bool need_fragmented = false;
    };



    Stream(_Stream s, Cfg cfg):_ptr(create(s, cfg)) {}

    ///Write to websocket
    /**
     * @param msg message to send
     * @param finish optional promise, which is set once the write operation finishes.
     *   This is the way how to prevent to bloating internal buffer if the
     *   data generation is too fast. (because write don't need to be co_awaited)
     * @return suspend point. If you ignore result, the write coroutine is started
     * immediately. Otherwise you can co_await the suspend point to make write
     * faster.
     *
     * @note To close the connection, send message type Type::connClose with filled
     * code and reason. By sending this message, the stream is considered half closed
     *
     * @note Function IS MT SAFE. Multiple threads can write, writing has internal
     * lock.
     */
    cocls::suspend_point<bool> write(const Message &msg, cocls::promise<void> finish) {
        return _ptr->write(msg, finish);
    }

    cocls::suspend_point<bool> write(const Message &msg) {
        return _ptr->write(msg);
    }

    ///Read from websocket
    /**
     * @return message received from the stream. The returned value is reference to
     * message, which is allocated inside of the object state. You need to process
     * the message before you can repeat the reading. Function is not MT Safe.
     *
     * @note The message Type::connClose is in most of cases the last message received
     * from the stream
     *
     * @note if the peer closes connection, or in case of timeout, the Type::connClose
     * is returned.
     *
     * @note Stream internally handles all required responses. It automatically responds
     * to Type::ping messages, and Type::connClose messages. In case of timeout, it
     * sends Type::ping and processes Type::pong. (it is required to set read timeout
     * to a reasonable value, for example 60 seconds)
     *
     * @note Function IS NOT MT SAFE.
     */
    cocls::future<Message> read() {
        return _ptr->read();
    }

    std::size_t get_buffered_size() const {
        return _ptr->get_buffered_size();
    }

    enum State {
        ///Stream is opened
        open,
        ///Stream is closing,
        closing,
        ///Stream is closed
        closed
    };

    State get_state() const {
        return _ptr->get_state();
    }

    ///close the stream explicitly
    cocls::suspend_point<bool> close() {
        return write(Message{{},Type::connClose,Base::closeNormal});
    }

    ///close the stream explicitly
    cocls::suspend_point<bool> close(std::uint16_t code) {
        return write(Message{{},Type::connClose,code});
    }


protected:

    class InternalState {
    public:

        InternalState(_Stream &s, Cfg &cfg)
            :_s(s)
            ,_reader(cfg.need_fragmented)
            ,_writer(s, cfg.client)
            ,_awt(*this)
            ,_awt_destroy(*this) {}

        cocls::suspend_point<bool> write(const Message &msg) {
            return _writer(msg);
        }
        cocls::suspend_point<bool> write(const Message &msg, cocls::promise<void> &p) {
            return _writer(msg, std::move(p));
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
            return _writer.get_buffered();
        }

        State get_state() const {
            bool wr_open = _writer;
            bool rd_open = !_closed;
            if (wr_open && rd_open) return State::open;
            if (!wr_open && !rd_open) return State::closed;
            return State::closing;
        }

        void destroy() {
            //write close message
            _writer({{},Type::connClose,Base::closeNormal});
            //sync to idle, then destroy
            _awt_destroy << [&]{return _writer.sync_for_idle();};
        }

    protected:

        cocls::suspend_point<void> on_read(cocls::future<std::string_view> &fut) noexcept { // @suppress("No return")
            try {
                std::string_view data = *fut;
                if (data.empty()) {
                    if (_ping_sent) {
                        Message m{"Ping timeout", Type::connClose, Base::closeAbnormal};
                        write(m);
                        return _read_promise(m);
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
                            _closed = true;
                            write({{}, Type::connClose, Base::closeNormal});
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
        Writer _writer;
        cocls::promise<Message> _read_promise;
        cocls::call_fn_future_awaiter<&InternalState::on_read> _awt;
        cocls::call_fn_future_awaiter<&InternalState::destroy_self> _awt_destroy;
        bool _ping_sent = false;
        bool _closed = false;
    };

    std::shared_ptr<InternalState> _ptr;

    struct Deleter {
        void operator()(InternalState *st) const {
            st->destroy();
        };
    };

    static std::shared_ptr<InternalState> create(_Stream &s, Cfg &cfg) {
        return std::shared_ptr<InternalState>(new InternalState(s, cfg), Deleter());
    }





};


}
}




#endif /* SRC_COROSERVER_WEBSOCKET_STREAM_H_ */
