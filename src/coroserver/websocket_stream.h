#ifndef SRC_COROSERVER_WEBSOCKET_STREAM_H_
#define SRC_COROSERVER_WEBSOCKET_STREAM_H_

#include "stream.h"
#include "websocket.h"
#include <coro.h>



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



    Stream(_Stream s, Cfg cfg);

    Stream() = default;


    coro::suspend_point<bool> write(const Message &msg);

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
    coro::future<Message> read();

    std::size_t get_buffered_size() const;

    enum State {
        ///Stream is opened
        open,
        ///Stream is closing,
        closing,
        ///Stream is closed
        closed
    };

    State get_state() const;

    ///close the stream explicitly
    coro::suspend_point<bool> close() {
        return write(Message{{},Type::connClose,Base::closeNormal});
    }

    ///close the stream explicitly
    coro::suspend_point<bool> close(std::uint16_t code) {
        return write(Message{{},Type::connClose,code});
    }

    coro::future<void> wait_for_flush();

    coro::future<void> wait_for_idle();

protected:

    class InternalState;
    struct Deleter;

    std::shared_ptr<InternalState> _ptr;


    std::shared_ptr<Stream::InternalState> create(_Stream &s, Cfg &cfg);


};


}
}




#endif /* SRC_COROSERVER_WEBSOCKET_STREAM_H_ */
