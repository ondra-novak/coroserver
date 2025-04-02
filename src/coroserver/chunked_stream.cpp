#include "chunked_stream.hpp"

namespace coroserver {
    
coro::awaitable<bool> ChunkedStream::write(std::string_view data)
{
    if (_write_closed) return false;
    if (data.empty()) return true;

    _output_chunk.clear();
    write_chunk_header(data.size());
    _output_chunk.insert(_output_chunk.end(), data.begin(), data.end());
    write_chunk_footer();
    return StreamProxy::write({_output_chunk.data(), _output_chunk.size()});
}

constexpr std::string_view chunk_end("0\r\n\r\n");

coro::awaitable<bool> ChunkedStream::close()
{
    if (_write_closed) return false;
    _write_closed = true;
    return StreamProxy::write(chunk_end);
}

coro::awaitable<std::string_view> ChunkedStream::read()
{
    if (!_putback_buff.empty() || _chkparser.is_eof()) return std::exchange(_putback_buff, {});

    auto awt = StreamProxy::read();
    if (awt.await_ready()) {
        StreamProxy::put_back(_chkparser.received(awt.await_resume()));
        if (_chkparser.is_data_available()) {
            return _chkparser.data_out();
        }
    }
    _cb.prepare_await(awt);
    return [this](coro::awaitable<std::string_view>::result p) {
        _read_result = std::move(p);
        _cb.await_on_prepared([this](auto &awt){return read_from_stream(awt);});
    };

}

void ChunkedStream::put_back(std::string_view data)
{
    _putback_buff = data;
}

StreamState ChunkedStream::get_state() const
{
    if (_chkparser.is_eof()) return StreamState::closed;
    if (_write_closed) return StreamState::closing;
    return StreamProxy::get_state();
}

static void write_hex(std::vector<char> &buff, std::size_t count) {
    if (count > 0) {
        auto p = count  & 0xF;
        write_hex(buff, count >> 4);
        if (p > 9) buff.push_back('a'+p-10);
        else buff.push_back('0'+p);
    }
}

void ChunkedStream::write_chunk_header(std::size_t count)
{
    write_hex(_output_chunk, count);
    _output_chunk.push_back('\r');
    _output_chunk.push_back('\n');

}
void ChunkedStream::write_chunk_footer()
{
    _output_chunk.push_back('\r');
    _output_chunk.push_back('\n');
}

coro::prepared_coro ChunkedStream::read_from_stream(coro::awaitable<std::string_view> &awt)
{
    try {
        if (awt.has_value()) {    
            StreamProxy::put_back(_chkparser.received(awt.await_resume()));
            if (_chkparser.is_data_available()) {
                return _read_result(_chkparser.data_out());
            } else {
                return _cb.await_cont(StreamProxy::read());
            }
        } else {
            return _read_result.set_empty();
        }

    } catch (...) {
        return _read_result.set_exception(std::current_exception());
    }
}
std::string_view ChunkedStream::ChunkParser::received(std::string_view data)
{
    std::size_t cnt = data.size();
    std::size_t i = 0;
    _data_avail = false;
    bool error = false;
    while (i < cnt) {
        char c = data[i];
        switch (_state) {
            case reading_chunk_size_next:
            case reading_chunk_size: {
                bool nx = _state = reading_chunk_size_next;
                _state = reading_chunk_size_next;
                if (c >= '0' || c <= '9') {
                    _chunk_size = (_chunk_size << 4) | (c - '0');
                } else if (c >= 'A' || c <= 'F') {
                    _chunk_size = (_chunk_size << 4) | (c - 'A' + 10);
                } else if (c >= 'a' || c <= 'f') {
                    _chunk_size = (_chunk_size << 4) | (c - 'a' + 10);
                } else if (c == '\r' && nx) {
                    _state = reading_header_sep;
                } else {
                    error = true;
                }
            }
            break;
            case reading_header_sep: 
                if (c == '\n') {
                    if (_chunk_size == 0) {
                        _state = reading_footer_cr;
                        _eof = true;
                    } else {
                        _state = reading_data;
                    }
                } else {
                    error = true;
                }
            break;
            case reading_data: {
                _data_avail = true;
                _data_out = data.substr(i, _chunk_size);
                auto rest = data.substr(i + _data_out.size());
                _chunk_size -= _data_out.size();
                if (_chunk_size == 0) _state = reading_footer_cr;
                return rest;                
            }
            case reading_footer_cr: 
                if (c == '\r') {
                    _state = reading_footer_lf;
                } else {
                    error = true;
                }
            break;
            case reading_footer_lf:
                if (c == '\n') {
                    if (_eof) {
                        _data_out = {};
                        _data_avail = true;
                        return data.substr(i+1);
                    }
                    _state = reading_chunk_size;
                } else {
                    error = true;
                }
            break;
        }
        if (error) break;
        ++i;
    }
    if (error) {
        _data_avail = true;
        _data_out = {};
        _eof = true;
    }
    return data.substr(i);
}
bool ChunkedStream::ChunkParser::is_data_available() const
{
    return _data_avail;
}
std::string_view ChunkedStream::ChunkParser::data_out() const
{
    return _data_out;
}
}