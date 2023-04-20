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
//write part
    cocls::suspend_point<void> join_write(cocls::future<bool> &f) noexcept;
    cocls::call_fn_future_awaiter<&ChunkedStream::join_write> _write_awt;
    std::string_view _data_to_write;
    std::string _new_chunk_write;
    cocls::promise<bool> _write_result;
    bool _eof_written = false;

//read part
    enum class ReadState {r1,n1,number,r2,n2,check_empty,data,r3,n3,eof};
    cocls::suspend_point<void> join_read(cocls::future<std::string_view> &f) noexcept;
    cocls::call_fn_future_awaiter<&ChunkedStream::join_read> _read_awt;
    cocls::promise<std::string_view> _read_result;
    ReadState _rd_state = ReadState::number;
    std::size_t _chunk_size = 0;




//    cocls::generator<std::string_view> _reader;


//    cocls::generator<std::string_view> start_reader();

//    bool _eof_reached = false;
};

}



#endif /* SRC_COROSERVER_CHUNKED_STREAM_H_ */
