/*
 * socket_stream.h
 *
 *  Created on: 25. 3. 2023
 *      Author: ondra
 */

#ifndef SRC_COROSERVER_SOCKET_STREAM_H_
#define SRC_COROSERVER_SOCKET_STREAM_H_

#include "async_engine.h"
#include "defs.h"
#include "stream.h"
#include <coro.h>


namespace coroserver {

class ContextIOImpl;


class SocketStream: public AbstractStreamWithMetadata {
public:
    SocketStream(AsyncResource *socket,
                 AsyncEngine engine,
                 PeerName peer,
                 TimeoutSettings tms);

    virtual coro::future<std::string_view> read() override;
    virtual std::string_view read_nb() override;
    virtual bool is_read_timeout() const override;
    virtual coro::future<bool> write(std::string_view buffer) override;
    virtual coro::future<bool> write_eof() override;
    virtual void shutdown() override;
    virtual Counters get_counters() const noexcept override;
    virtual PeerName get_peer_name() const override;

    static Stream create(AsyncResource *socket,
            AsyncEngine engine,
            PeerName peer,
            TimeoutSettings tms);
    virtual ~SocketStream();




protected:

    AsyncResource *_socket;
    AsyncEngine _engine;
    Counters _cntr;
    PeerName _peer;

    coro::future<int> _wait_read_result;
    coro::future<int> _wait_write_result;
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
    void enable_nagle();
    void disable_nagle();
};

}


#endif /* SRC_COROSERVER_SOCKET_STREAM_H_ */
