/*
 * socket_stream.h
 *
 *  Created on: 25. 3. 2023
 *      Author: ondra
 */

#ifndef SRC_COROSERVER_SOCKET_STREAM_H_
#define SRC_COROSERVER_SOCKET_STREAM_H_

#include "defs.h"
#include "stream.h"
#include <cocls/generator.h>
#include "socket_support.h"

namespace coroserver {

class ContextIOImpl;



class SocketStream: public AbstractStreamWithMetadata {
public:
    SocketStream(SocketSupport context, SocketHandle h, PeerName peer, TimeoutSettings tms);
    ~SocketStream();
    virtual cocls::future<std::string_view> read() override;
    virtual std::string_view read_nb() override;
    virtual bool is_read_timeout() const override;
    virtual cocls::future<bool> write(std::string_view buffer) override;
    virtual cocls::future<bool> write_eof() override;
    virtual cocls::suspend_point<void> shutdown() override;
    virtual Counters get_counters() const noexcept override;
    virtual PeerName get_peer_name() const override;

protected:
    SocketSupport _ctx;
    SocketHandle _h;
    Counters _cntr;
    PeerName _peer;
    cocls::generator<std::string_view> _reader;
    cocls::generator<bool, std::string_view> _writer; //writer

    std::vector<char> _read_buffer;
    bool _is_timeout = false;
    bool _is_eof = false;
    bool _is_closed = false;
    std::size_t _last_read_full = 0;
    std::size_t _new_buffer_size = 1024;



    cocls::generator<std::string_view> start_read();
    cocls::generator<bool, std::string_view> start_write();
};

}


#endif /* SRC_COROSERVER_SOCKET_STREAM_H_ */
