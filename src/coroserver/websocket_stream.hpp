#pragma once

#include "buffered_stream.hpp"
#include "ws_defs.hpp"
#include <random>

namespace coroserver {

namespace ws {

class WebSocketStream {
public:

    WebSocketStream(Stream s, bool server = false, bool need_fragmented = false);
    WebSocketStream(BufferedStream s, bool server = false, bool need_fragmented = false);
    ~WebSocketStream();

    StreamState get_state() const {        
        if (_close_recv) return StreamState::closed;
        if (_close_sent) return StreamState::closing;
        return _stream.get_state();
    }

    ///Enables reporting of timeouts
    /** By default, read timeout automatically generates ping, and doesn't return
     * control to the caller. If you want to know about timeout, you need to
     * set this flag to true. The read() function will cancel awaiting operation
     * in case of timeout. The ping is not sent in this case.
     * @param need_timeout true, if you want to know about timeouts
     */
    void set_need_timeout(bool need_timeout) {
        _need_timeout = need_timeout;
    }
    ///Returns true, if the timeout is needed
    /**
     * @return true, if the timeout is needed
     */
    bool get_need_timeout() const {
        return _need_timeout;
    }
    ///Read data from the stream
    /**
     * @return awaitable carrying received message
     * 
     * This function reads data from the stream. The function returns
     * a message. The returned message can be of following types: text, binary and close. 
     * The ping and pong messages are not returned. Pings are automatically
     * answered by pong messages. The close message is automatically
     * answered by close message. However the close message is also
     * returned to the caller. If the stream is closed prematurely, the function
     * returns close message with abnormal close status
     * 
     * @note only one awaitable can be active at the same time. You need to
     * use coro::mutex if you need to call read() from multiple coroutines.
     */
     
     
    awaitable<Message> read();

    ///write the message
    /**
     * @param message message to write
     * @retval true success
     * @retval false error, stream closed
     * 
     * You can write any type of the message you need. This includes ping
     * and pong messages, however the pong messages are never returned by the
     * read() function. So use ping only to ensure other side, that connection
     * is still alive
     * 
     * By sending close message, the stream is mnarked as closing.
     * 
     * @note the function is mt safe, you can call it from multiple threads
     */
    bool write(const Message &message);

    bool close(std::uint16_t code = Base::closeNormal, std::string_view message = {});

    ///Sets websocket stream timeouts
    /**
     * @param timeout timeouts
     * @note The read timeout defines period of sending ping messages when there is no
     * data. The write timeout defines period of waiting for the data to be sent. Any
     * timeout during write causes stream to be closed.
     */
    void set_timeouts(IOTimeout timeout) { 
        _stream.set_timeouts(timeout);
    }
    IOTimeout getIOTimeouts() const {
        return _stream.get_timeouts();
    }
    Stream::Counters getCounters() const {
        return _stream.get_counters();
    }

    std::size_t get_buffered_size() const {
        return _stream.get_buffered_size();
    }

    ///Await until all buffered data are sent
    /**
     * This function waits until all buffered data are sent. The function
     * returns true, if all data are sent. If the stream is closed, the function
     * returns false.
     *
     * @return awaitable
     * @retval true all data are sent
     * @retval false stream is closed
     * 
     * @note multiple awaits at the same time are supported.
     */

    awaitable<bool> flush() {
        return _stream.flush();
    }

protected:
    ///WebSocket stream
    /**
     * This class is used to send and receive WebSocket messages. It uses
     * buffered stream to read and write data. The data are encoded in the
     * WebSocket format.
     */
    BufferedStream _stream;
    std::mt19937 _rand_gen =  {};

    std::vector<char> _buffer;
    Parser<std::vector<char> > _parser;
    bool _server = false;
    bool _ping_sent = false;
    bool _close_recv = false;
    bool _close_sent = false;
    bool _need_timeout = false;


    coro::awaiting_callback<awaitable<std::string_view>, 
            WebSocketStream *, awaitable<Message>::result > _read_callback;


    ///Handle message   
    /**
     * This function is called when a message is received. It can be used to
     * handle the message and return true if the message was handled.
     * @param msg message to handle
     * @return true if the message was handled, false otherwise
     */
    bool handle_message(const Message &msg);

    std::array<std::uint8_t, 4> generate_masking_key();

};

}

}