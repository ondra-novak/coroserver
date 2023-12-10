#include "chunked_stream.h"

#include "character_io.h"

namespace coroserver {

ChunkedStream::ChunkedStream(std::shared_ptr<IStream> proxied, bool allow_read, bool allow_write)
:AbstractProxyStream(std::move(proxied))
,_write_awt(*this)
, _eof_written (!allow_write)
, _read_awt(*this)
,_rd_state (allow_read?ReadState::number:ReadState::eof)
{


}

coro::future<std::string_view> ChunkedStream::read() {
    auto buff = read_putback_buffer();
    if (!buff.empty() || _rd_state == ReadState::eof) return coro::future<std::string_view>::set_value(buff);
    return [&](coro::promise<std::string_view> p) {
        _read_result = std::move(p);
        _read_awt << [&]{return _proxied->read();};
    };

}

coro::suspend_point<void> ChunkedStream::join_read(coro::future<std::string_view> &f) noexcept {

    auto error = []{
            throw std::runtime_error("Invalid chunk format");
    };

    auto next_state = [](ReadState &rd) {
        rd = static_cast<ReadState>(static_cast<int>(rd)+1);
    };

    try {
        std::string_view buff = f.value();
        if (buff.empty()) {
            _rd_state = ReadState::eof;
            _read_result(buff);
        }
        auto itr = buff.begin();
        auto beg = itr;
        auto end = buff.end();
        while (itr != end) {
            switch(_rd_state) {
                case ReadState::data: if (_chunk_size) {
                        std::string_view result(buff.data()+std::distance(beg, itr), std::distance(itr,end));
                        std::string_view out = result.substr(0,_chunk_size);
                        _proxied->put_back(result.substr(out.size()));
                        _chunk_size-=out.size();
                        return _read_result(out);
                    } else {
                        _rd_state = ReadState::r1;
                    }break;
                case ReadState::r1:
                case ReadState::r2:
                case ReadState::r3:
                    if (*itr != '\r') error();
                    ++itr;
                    next_state(_rd_state);
                    break;
                case ReadState::n1:
                case ReadState::n2:
                case ReadState::n3:
                    if (*itr != '\n') error();
                    ++itr;
                    next_state(_rd_state);
                    break;
                case ReadState::number: {
                        int n = 0;
                        switch (*itr) {
                            case '0':n = 0;break;
                            case '1':n = 1;break;
                            case '2':n = 2;break;
                            case '3':n = 3;break;
                            case '4':n = 4;break;
                            case '5':n = 5;break;
                            case '6':n = 6;break;
                            case '7':n = 7;break;
                            case '8':n = 8;break;
                            case '9':n = 9;break;
                            case 'A':n = 10;break;
                            case 'B':n = 11;break;
                            case 'C':n = 12;break;
                            case 'D':n = 13;break;
                            case 'E':n = 14;break;
                            case 'F':n = 15;break;
                            case 'a':n = 10;break;
                            case 'b':n = 11;break;
                            case 'c':n = 12;break;
                            case 'd':n = 13;break;
                            case 'e':n = 14;break;
                            case 'f':n = 15;break;
                            default:
                                next_state(_rd_state);
                                continue;
                        }
                        _chunk_size = (_chunk_size << 4) | n;
                        ++itr;
                    }break;
                case ReadState::check_empty: {
                    if (_chunk_size == 0) {
                        _rd_state = ReadState::r3;
                    } else {
                        _rd_state = ReadState::data;
                    }
                    break;
                case ReadState::eof: {
                        std::string_view pb(buff.data()+std::distance(beg, itr), std::distance(itr,end));
                        _proxied->put_back(pb);
                        return _read_result();
                }
                default:
                    error();
                    break;
                }
            }
        }

        if (_rd_state == ReadState::eof) return _read_result();

        _read_awt << [&]{return _proxied->read();};
        return {};



    } catch (...) {
        return _read_result(std::current_exception());
    }
}



static void hex2str(std::size_t sz, std::string &out) {
    static const char hextbl[16] = {'0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'};
    if (sz) {
        hex2str(sz>>4, out);
        out.push_back(hextbl[sz & 0xF]);
    }
}

coro::future<bool> ChunkedStream::write(std::string_view buffer) {
    if (_eof_written) return coro::future<bool>::set_value(false);
    assert(_data_to_write.empty() && "Write is still pending");
    _data_to_write = buffer;
    hex2str(buffer.size(), _new_chunk_write);
    _new_chunk_write.append("\r\n");
    return [&](coro::promise<bool> p) {
        _write_result = std::move(p);
        _write_awt << [&]{return _proxied->write(_new_chunk_write);};
    };
}

coro::suspend_point<void> ChunkedStream::join_write(coro::future<bool> &f) noexcept {
    try {
        bool res = f.value();
        if (res && !_data_to_write.empty()) {
            _new_chunk_write.clear();
            _new_chunk_write.append("\r\n");
            auto d = _data_to_write;
            _data_to_write = {};
            _write_awt << [&]{return _proxied->write(d);};
            return {};
        } else {
            return _write_result(res);
        }
    } catch (...) {
        return _write_result(std::current_exception());
    }
}

coro::future<bool> ChunkedStream::write_eof() {
    _eof_written = true;
    _new_chunk_write.append("0\r\n\r\n");
    return [&](coro::promise<bool> p) {
        _write_result = std::move(p);
        _write_awt << [&]{return _proxied->write(_new_chunk_write);};
    };
}
#if 0
coro::generator<std::string_view> ChunkedStream::start_reader() {
    std::size_t chunk_size = 0;
    bool next_chunk = false;
    unsigned int hex_table[32] = {
          //0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
            0,10,11,12,13,14,15,16, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0
    };

    auto error = []{
            throw std::runtime_error("Invalid chunk format");
    };

    while (true) {
        if (chunk_size) {
            std::string_view buff = co_await _proxied->read();
            std::string_view res = buff.substr(0,chunk_size);
            _proxied->put_back(buff.substr(res.size()));
            chunk_size-=res.size();
            co_yield res;
        } else {
            CharacterReader<Stream> rd(_proxied);
            if (next_chunk) {
                if ('\r' != co_await rd) error();
                if ('\n' != co_await rd) error();
            }
            int c = co_await rd;
            while (std::isxdigit(c)){
                chunk_size = chunk_size * 16 + hex_table[c & 0x1F];
                c = co_await rd;
            }
            if (c != '\r') error();
            c = co_await rd;
            if (c != '\n') error();
            next_chunk = true;
            if (!chunk_size) {
                _eof_reached = true;
                while (true) {
                    co_yield std::string_view();
                }
            }
        }
    }
}
#endif
Stream ChunkedStream::read(Stream target) {
    return Stream(std::make_shared<ChunkedStream>(target.getStreamDevice(), true, false));
}

Stream ChunkedStream::write(Stream target) {
    return Stream(std::make_shared<ChunkedStream>(target.getStreamDevice(), false, true));
}

Stream ChunkedStream::read_and_write(Stream target) {
    return Stream(std::make_shared<ChunkedStream>(target.getStreamDevice(), true, true));
}

ChunkedStream::~ChunkedStream() {
    if (_rd_state != ReadState::eof && !_eof_written) {
        _proxied->shutdown();
    }
}



}
