/*
 * socket_stream.h
 *
 *  Created on: 25. 3. 2023
 *      Author: ondra
 */

#ifndef SRC_COROSERVER_SOCKET_STREAM_H_
#define SRC_COROSERVER_SOCKET_STREAM_H_

#include "async_support.h"
#include "defs.h"
#include "stream.h"
#include <coro.h>


namespace coroserver {

class ContextIOImpl;


enum SocketStreamPurpose {
    ///Socket is used for interactive session (such a telnet) - this is default
    /**
     * This option can save network bandwith by collecting small packets, however
     * response latency can be highter
     * In this case, TCP_QUICKACK is enabled and TCP_NODELAY is disabled
     *
     */
    interactive,
    ///Socket is used for streaming
    /**
     * This option reduces count of ACKS however it expects using of a buffered
     * write
     *
     * In this case, TCP_QUICKACK is disabled and TCP_NODELAY is enabled
     */
    streaming,
    ///Socket is used realtime communication
    /**
     * This option is best for latency, but also takes wide bandwith
     *
     * In this case, TCP_QUICKACK is enabled and TCP_NODELAY is enabled
     */
    realtime,
    ///Socket is used for notification
    /**
     * This option greatly saves a network bandwith, it delays acks and enables
     * Nagle. However the latency is the hightest
     */
    notification,
};

class SocketStream: public AbstractStreamWithMetadata {
public:
    SocketStream(AsyncSocket socket, PeerName peer, TimeoutSettings tms);

    virtual coro::future<std::string_view> read() override;
    virtual std::string_view read_nb() override;
    virtual bool is_read_timeout() const override;
    virtual coro::future<bool> write(std::string_view buffer) override;
    virtual coro::future<bool> write_eof() override;
    virtual void shutdown() override;
    virtual Counters get_counters() const noexcept override;
    virtual PeerName get_peer_name() const override;

    static Stream create(AsyncSocket socket, PeerName peer, TimeoutSettings tms);
    virtual ~SocketStream();




protected:

    AsyncSocket _socket;
    Counters _cntr;
    PeerName _peer;

    coro::future<bool> _wait_read_result;
    coro::future<bool> _wait_write_result;
    coro::future<bool>::target_type _wait_read_target;
    coro::future<bool>::target_type _wait_write_target;
    coro::promise<std::string_view> _read_promise;
    coro::promise<bool> _write_promise;

    std::vector<char> _read_buffer;
    std::string_view _write_buffer;
    std::atomic_flag _nagle_state;

    bool _is_eof = false;
    bool _is_closed = false;
    std::size_t _last_read_full = 0;
    std::size_t _new_buffer_size = 1024;

    void write_begin();
    bool read_begin(std::string_view &buff);
    void read_completion(coro::future<bool> *f) noexcept;
    void write_completion(coro::future<bool> *f) noexcept;
    void enable_nagle();
    void disable_nagle();
};

}


#endif /* SRC_COROSERVER_SOCKET_STREAM_H_ */
