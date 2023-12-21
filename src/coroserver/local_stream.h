/*
 * local_stream.h
 *
 *  Created on: 17. 12. 2023
 *      Author: ondra
 */

#ifndef SRC_COROSERVER_LOCAL_STREAM_H_
#define SRC_COROSERVER_LOCAL_STREAM_H_



#include "async_support.h"
#include "defs.h"
#include "stream.h"
#include <coro.h>


namespace coroserver {

class Context;


class LocalStream: public AbstractStreamWithMetadata {
public:
    LocalStream(AsyncSocket read_fd,  AsyncSocket write_fd, PeerName peer, TimeoutSettings tms);

    virtual coro::future<std::string_view> read() override;
    virtual std::string_view read_nb() override;
    virtual bool is_read_timeout() const override;
    virtual coro::future<bool> write(std::string_view buffer) override;
    virtual coro::future<bool> write_eof() override;
    virtual void shutdown() override;
    virtual Counters get_counters() const noexcept override;
    virtual PeerName get_peer_name() const override;




protected:

    AsyncSocket _read_fd;
    AsyncSocket _write_fd;
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

    bool read_available() const;
    bool write_available() const;

};



}




#endif /* SRC_COROSERVER_LOCAL_STREAM_H_ */

