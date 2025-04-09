#pragma once

#include <vector>
#include "stream.hpp"

namespace coroserver {

class ChunkedStream : public StreamProxy{
public:

    using StreamProxy::StreamProxy;

    virtual coro::awaitable<bool> write(std::string_view data) override;
    virtual coro::awaitable<bool> close() override;
    virtual coro::awaitable<std::string_view> read() override;
    virtual void put_back(std::string_view data) override;
    virtual StreamState get_state() const override;

    ///Create chunked stream reader and writer
    /**
     * @parm src source stream
     *
     * By writting to the stream, each data block is wrapped into valid chunk. To write end
     * chunk, just call close();
     *
     * Reading from the stream causes that chunk marks are removed from the data. The read()
     * returns eof if end chunk is extracted
     */


    static Stream create(Stream src) {
        return Stream(std::make_shared<ChunkedStream>(src));
    }

protected:


    std::vector<char> _output_chunk;

    bool _write_closed = false;

    void write_chunk_header(std::size_t count);
    void write_chunk_footer();

    class ChunkParser {
    public:
        std::string_view received(std::string_view data);
        bool is_data_available() const;
        std::string_view data_out() const;
        bool is_eof() const {return _eof;}

    protected:
        enum State {
            reading_chunk_size,
            reading_chunk_size_next,
            reading_header_sep,
            reading_data,
            reading_footer_cr,
            reading_footer_lf
        };
        State _state = reading_chunk_size;
        std::size_t _chunk_size;
        std::string_view _data_out;
        bool _data_avail = false;
        bool _eof = false;
    };

    ChunkParser _chkparser;
    std::string_view _putback_buff;
    coro::awaiting_callback<coro::awaitable<std::string_view>, ChunkedStream *> _cb;
    coro::awaitable<std::string_view>::result _read_result;

    coro::prepared_coro read_from_stream(coro::awaitable<std::string_view> &awt);
};


}
