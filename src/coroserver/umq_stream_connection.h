/*
 * umq_stream_connection.h
 *
 *  Created on: 25. 12. 2023
 *      Author: ondra
 */

#ifndef SRC_COROSERVER_UMQ_STREAM_CONNECTION_H_
#define SRC_COROSERVER_UMQ_STREAM_CONNECTION_H_
#include "umq_connection.h"

#include "stream.h"

#include "mt_stream.h"
namespace coroserver {

namespace umq {

/*
 *
 * +--------+--------------------------------+---------------------------
 * |        |                                |
 * |  start |   payload_size                 |          payload...
 * |        |                                |
 * +--------+--------------------------------+---------------------------
 *
 * start = <type 2b>|<size 3b>
 *
 * type = 00 (0x00) = ping
 *        01 (0x08) = pong
 *        10 (0x10) = text frame
 *        11 (0x18) = binary frame
 *
 * size = count of bytes to store payload_size minus 1
 *        0x0 - 1 byte
 *        0x1 - 2 bytes
 *        0x7 - 8 bytes
 *
 * payload_size = big-endian size of payload
 *
 * payload - message payload of payload_size bytes
 *
 *
 *
 */

class StreamConnection: public IConnection {
public:
    StreamConnection(Stream stream);

    virtual coro::future<Message> receive() override;
    virtual coro::lazy_future<bool> send(const coroserver::umq::Message &msg) override;

protected:

    MTStreamWriter _mtwrite;

    coro::promise<Message> _rdmsg;
    coro::future<std::string_view> _fut;
    coro::future<std::string_view>::target_type _target;

    enum class PkgType : char {
        ping   = 0x00,
        pong   = 0x08,
        text   = 0x10,
        binary = 0x18
    };

    enum class Phase {
        header,
        size,
        payload
    };

    Phase _phase = Phase::header;
    std::vector<char> _buffer;


    PkgType _nxt_type = PkgType::pong;

    std::size_t _rdmsgsz = 0;
    unsigned char _rdmsgszsz = 0;

    bool _binary = false;
    bool _hdr = false;
    bool _pinged = false;


    void on_data(coro::future<std::string_view> *fut);

    coro::lazy_future<bool> send(PkgType type, const std::string_view &data);
    static unsigned char calc_bytes(std::uint64_t sz);
    std::uint64_t get_pkg_total_size() const;
    static unsigned int get_hdr_size(unsigned char hdr_first_byte);
    void process_pkg();
    void init_read();

};


}

}



#endif /* SRC_COROSERVER_UMQ_STREAM_CONNECTION_H_ */
