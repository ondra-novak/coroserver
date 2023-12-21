/*
 * message_stream.h
 *
 *  Created on: 11. 5. 2023
 *      Author: ondra
 */

#ifndef SRC_COROSERVER_MESSAGE_STREAM_H_
#define SRC_COROSERVER_MESSAGE_STREAM_H_

#include "stream.h"

namespace coroserver {

class MessageStream: public AbstractProxyStream {

};
#if 0













///MessageStream is message oriented stream
/**
 * This stream transfers whole messages. In compare to standard streams, where sending
 * a sequence of bytes doesn't mean, that the sequence bytes will be received as the same
 * group. Standard stream doesn't guaranteed grouping data into a "messages", so the bytes
 * can be fragmented or combined as this is required by underlying network subsystem. The message
 * stream on the other hand guarantees the sequence of bytes is received as the same group
 * as it was sent. However it also means, the read function will always returns a complete
 *  message, it cannot return partially received message
 *
 * The messages are send in the following format
 *
 * +-------+--------------------------------------------+
 * | size  |                   message                  |
 * +-------+--------------------------------------------+
 *
 * The size part is variable and it is encoded as sequence of 7 bit numbers where
 * the eight (MSB) bit is reserved as continuation flag. So the function which
 * reads the size just collects all bytes with MSB flag set to 1, and stops this when
 * byte with MSB flag is set to zero. This byte is the last byte of the size. The
 * bytes are ordered in network order - big endian.
 *
 * For example number 0x12345 is written as
 *
 * 1    2    3    4    5
 * 0001 0010 0011 0100 0101
 *
 * 10000100 11000110 01000101
 *  84       C6          45
 */
class MessageStream: public AbstractProxyStream {
public:

    MessageStream(const std::shared_ptr<IStream> &proxied, std::size_t max_msg_size);

    virtual coro::future<std::string_view> read() override;
    virtual coro::future<bool> write(std::string_view buffer) override;
    virtual coro::future<bool> write_eof() override;

    static Stream create(const Stream &target, std::size_t max_msg_size = std::numeric_limits<std::size_t>::max());

protected:
    std::size_t _max_message_size = 0;

    std::vector<char> _read_msg_buffer;
    std::size_t _read_size = 0;
    bool _reading_size = true;
    coro::promise<std::string_view> _read_result;
    coro::suspend_point<void> on_read(coro::future<std::string_view> &f) noexcept;
    coro::call_fn_future_awaiter<&MessageStream::on_read> _read_awt;

    std::string _write_msg_buffer;
    std::string_view _write_payload;
    coro::suspend_point<void> on_write(coro::future<bool> &f) noexcept;
    coro::promise<bool> _write_result;
    coro::call_fn_future_awaiter<&MessageStream::on_write> _write_awt;
    void encode_size(std::size_t sz, bool first = true);



};

#endif


}



#endif /* SRC_COROSERVER_MESSAGE_STREAM_H_ */
