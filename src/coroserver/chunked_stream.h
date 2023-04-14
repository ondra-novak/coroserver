/*
 * chunked_stream.h
 *
 *  Created on: 14. 4. 2023
 *      Author: ondra
 */

#ifndef SRC_COROSERVER_CHUNKED_STREAM_H_
#define SRC_COROSERVER_CHUNKED_STREAM_H_
#include "stream.h"

#include <cocls/generator.h>

namespace coroserver {

///Chunked stream
/**
 * Creates or read chunked stream which is substream of existing stream
 *
 * To write valid chunked stream, you need to write_eof() after writting the data
 * To read chunked stream, you need to read whole stream, or discard rest of
 * unprocessed data. Breaking this rule can cause breaking connection
 * on target stream during destruction of this object
 */
class ChunkedStream: public AbstractProxyStream {
public:

    ChunkedStream(std::shared_ptr<IStream> proxied, bool allow_read, bool allow_write);
    virtual cocls::future<std::string_view> read() override;
    virtual cocls::future<bool> write(std::string_view buffer) override;
    virtual cocls::future<bool> write_eof() override;

    ///Create chunked stream for reading
    static Stream read(Stream target);
    ///Create chunked stream for writing
    static Stream write(Stream target);
    ///Create chunked stream for both reading and writing
    static Stream read_and_write(Stream target);

    ~ChunkedStream();

protected:

    cocls::generator<std::string_view> _reader;
    cocls::generator<bool, std::string_view> _writer;


    cocls::generator<std::string_view> start_reader();
    cocls::generator<bool, std::string_view> start_writer();

    bool _eof_reached = false;
    bool _eof_writen = false;
};

}



#endif /* SRC_COROSERVER_CHUNKED_STREAM_H_ */
