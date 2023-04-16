/*
 * limited_stream.h
 *
 *  Created on: 15. 4. 2023
 *      Author: ondra
 */

#ifndef SRC_COROSERVER_LIMITED_STREAM_H_
#define SRC_COROSERVER_LIMITED_STREAM_H_

#include "stream.h"

#include <cocls/generator.h>

namespace coroserver {

///Limited stream
class LimitedStream: public AbstractProxyStream {
public:

    LimitedStream(std::shared_ptr<IStream> proxied,
                std::size_t limit_read,
                std::size_t limit_write);

    virtual cocls::future<std::string_view> read() override;
    virtual cocls::future<bool> write(std::string_view buffer) override;
    virtual cocls::future<bool> write_eof() override;

    ///Create chunked stream for reading
    static Stream read(Stream target, std::size_t limit_read);
    ///Create chunked stream for writing
    static Stream write(Stream target, std::size_t limit_read);
    ///Create chunked stream for both reading and writing
    static Stream read_and_write(Stream target, std::size_t limit_read, std::size_t limit_write);

    ~LimitedStream();

protected:

    cocls::generator<std::string_view> _reader;
    cocls::generator<bool, std::string_view> _writer;


    cocls::generator<std::string_view> start_reader();
    cocls::generator<bool, std::string_view> start_writer();


    std::size_t _limit_read;
    std::size_t _limit_write;



    cocls::malleable_awaiter _awt;
    cocls::future<std::string_view> _rdfut;
    cocls::promise<std::string_view> _fltfut;
};



}





#endif /* SRC_COROSERVER_LIMITED_STREAM_H_ */
