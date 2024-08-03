#ifndef SRC_COROSERVER_WEBSOCKET_STREAM_H_
#define SRC_COROSERVER_WEBSOCKET_STREAM_H_

#include "stream.h"
#include "websocket.h"
#include <coro.h>



namespace coroserver {

namespace ws {

using _Stream = Stream;

class StreamImpl {
public:

    struct Cfg {
        bool client = false;
        bool need_fragmented = false;
    };


    StreamImpl(_Stream s, const Cfg &cfg);
   
    ///read message
    /** 
     * @param received message
     * @note not MT Safe, not concurrent safe,only one pending reading at time
     */
    coro::future<Message> receive();

    bool send(const Message &msg, coro::promise<bool> completion = {});

    ///shutdown reading, read immediately returns connClose
    void shutdown();

    coro::future<bool> send_close(unsigned int code = Base::closeNormal);

    bool is_closed() const;


    ~StreamImpl();
protected:
    _Stream _s;
    Cfg _cfg;
    Parser _parser;
    Builder _builder;

    coro::future<std::string_view> _rdfut;
    coro::future<bool> _wrfut;
    std::mutex _mx;
    std::vector<char> _wrbuff;
    std::vector<char> _sendbuff;
    std::vector<coro::promise<bool> > _wrcompl;
    std::vector<coro::promise<bool> > _sendcompl;
    bool _pending = false;
    bool _closed = false;
    bool _pingsent = false;

    void parse_msg(coro::promise<Message> rdprom);
    void complete_write();
};

class Stream {
public:
    using Cfg = StreamImpl::Cfg;

    Stream() = default;

    Stream(std::shared_ptr<StreamImpl> ptr):_ptr(ptr) {}
    ///Receive message
    /** @return received message (co_await)
     *  - returns any received message including ping and pong messages
     *  - in case of stream timeout, sends ping and expects pong (ping interval = read timeout)
     *  - no ping in interval closes the stream
     *  - any close of the stream is reported as Type::connClose
     *  @note only one pending read is allowed at the time
     */
    coro::future<Message> receive() {return _ptr->receive();}
    
    ///Send message
    /** 
     * @param msg message to send
     * @param completion optional place promise her if you need to receive notify, that message has
     * been sent (flushed from buffer). If not filled, no notify is delivered
     * @retval true successfully enqueued
     * @retval false stream is closed, message discarded
     * @note if completion is set, it is always resolved with status
     * @note once the stream is closed, no more messages can be send
     * @note this function is MT Safe, multiple threads can post messages
     */ 
    bool send(const Message &msg, coro::promise<bool> completion = {}) {return _ptr->send(msg, std::move(completion));}
    
    ///Shutdown stream, unblock any awaiting future
    /**
     * Forces to close stream (no message is sent), reader is resolved with connClose, writing
     * is resolved with false. This function is synchronous (can be called from destructor). Once
     * stream is in shutdown state, no more message can be received or send
     */
    void shutdown() {return _ptr->shutdown();}
    ///Sends close message
    /** 
     * @param code close code
     * @return awaitable result which is resolved, once close is successfully posted from the buffer
     * @retval true sent
     * @retval false the stream was already closed
     */
    coro::future<bool> send_close(unsigned int code = Base::closeNormal) {return _ptr->send_close(code);}
    
    ///Determines whether stream is closed
    bool is_closed() const {return _ptr->is_closed();}

    ///create stream
    /** 
     * @param s TCP stream
     * @param cfg config
     */
    static Stream create(_Stream s,const Cfg &cfg);

    explicit operator bool() const {
        return _ptr != nullptr;
    }

protected:
    std::shared_ptr<StreamImpl> _ptr;
};

}

}




#endif /* SRC_COROSERVER_WEBSOCKET_STREAM_H_ */
