#include "chunked_stream.h"

#include "character_io.h"

#include <cassert>
namespace coroserver {

ChunkedStream::ChunkedStream(std::shared_ptr<IStream> proxied, bool allow_read, bool allow_write)
:AbstractProxyStream(std::move(proxied))
, _eof_written (!allow_write)
,_rd_state (allow_read?ReadState::number:ReadState::eof)
{
    coro::target_member_fn_activation<&ChunkedStream::join_read>(_read_fut_target, this);
    coro::target_member_fn_activation<&ChunkedStream::join_write>(_write_fut_target, this);

}

coro::future<std::string_view> ChunkedStream::read() {
    auto buff = read_putback_buffer();
    if (!buff.empty() || _rd_state == ReadState::eof) return buff;
    return [&](coro::promise<std::string_view> p) {
        _read_result = std::move(p);
        _read_fut << [&]{return _proxied->read();};
        _read_fut.register_target(_read_fut_target);
    };

}

void ChunkedStream::join_read(coro::future<std::string_view> *f) noexcept {

    auto error = []{
            throw std::runtime_error("Invalid chunk format");
    };

    auto next_state = [](ReadState &rd) {
        rd = static_cast<ReadState>(static_cast<int>(rd)+1);
    };

    try {
        std::string_view buff = *f;
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
                        _read_result(out);
                        return;
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
                        _read_result();
                        return;
                }
                default:
                    error();
                    break;
                }
            }
        }

        if (_rd_state == ReadState::eof) {
            _read_result();
            return;
        }

        _read_fut << [&]{return _proxied->read();};
        _read_fut.register_target(_read_fut_target);

    } catch (...) {
        _read_result.reject();
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
    if (_eof_written) return false;
    assert(_data_to_write.empty() && "Write is still pending");
    _data_to_write = buffer;
    hex2str(buffer.size(), _new_chunk_write);
    _new_chunk_write.append("\r\n");
    return [&](coro::promise<bool> p) {
        _write_result = std::move(p);
        _write_fut << [&]{return _proxied->write(_new_chunk_write);};
        _write_fut.register_target(_write_fut_target);
    };
}

void ChunkedStream::join_write(coro::future<bool> *f) noexcept {
    try {
        bool res = *f;
        if (res && !_data_to_write.empty()) {
            _new_chunk_write.clear();
            _new_chunk_write.append("\r\n");
            auto d = _data_to_write;
            _data_to_write = {};
            _write_fut << [&]{return _proxied->write(d);};
            _write_fut.register_target(_write_fut_target);
        } else {
            _write_result(res);
        }
    } catch (...) {
        _write_result.reject();
    }
}

coro::future<bool> ChunkedStream::write_eof() {
    _eof_written = true;
    _new_chunk_write.append("0\r\n\r\n");
    return [&](coro::promise<bool> p) {
        _write_result = std::move(p);
        _write_fut << [&]{return _proxied->write(_new_chunk_write);};
        _write_fut.register_target(_write_fut_target);
    };
}

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
