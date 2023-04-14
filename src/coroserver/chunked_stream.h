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

class ChunkedStream: public AbstractProxyStream {
public:

    ChunkedStream(std::shared_ptr<IStream> proxied, bool allow_read, bool allow_write);
    virtual cocls::future<std::string_view> read() override;
    virtual cocls::future<bool> write(std::string_view buffer) override;
    virtual cocls::future<bool> write_eof() override;

protected:

    cocls::generator<std::string_view> _reader;
    cocls::generator<bool, std::string_view> _writer;


    cocls::generator<std::string_view> start_reader();
    cocls::generator<bool, std::string_view> start_writer();

    std::string_view _done_empty;
    bool _done_false = false;
};

}



#endif /* SRC_COROSERVER_CHUNKED_STREAM_H_ */
