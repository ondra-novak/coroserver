/*
 * limited_stream.h
 *
 *  Created on: 15. 4. 2023
 *      Author: ondra
 */

#ifndef SRC_COROSERVER_LIMITED_STREAM_H_
#define SRC_COROSERVER_LIMITED_STREAM_H_

#include "stream.h"

#include <coro.h>


namespace coroserver {

///Limited stream
class LimitedStream: public AbstractProxyStream {
public:

    LimitedStream(std::shared_ptr<IStream> proxied,
                std::size_t limit_read,
                std::size_t limit_write);

    virtual coro::future<std::string_view> read() override;
    virtual coro::future<bool> write(std::string_view buffer) override;
    virtual coro::future<bool> write_eof() override;

    ///Create chunked stream for reading
    static Stream read(Stream target, std::size_t limit_read);
    ///Create chunked stream for writing
    static Stream write(Stream target, std::size_t limit_write);
    ///Create chunked stream for both reading and writing
    static Stream read_and_write(Stream target, std::size_t limit_read, std::size_t limit_write);



    ~LimitedStream();

protected:


    std::size_t _limit_read;
    std::size_t _limit_write;

    coro::suspend_point<void> join_read(coro::future<std::string_view> &fut) noexcept;
    coro::call_fn_future_awaiter<&LimitedStream::join_read> _read_awt;
    coro::promise<std::string_view> _read_result;
};



}





#endif /* SRC_COROSERVER_LIMITED_STREAM_H_ */
