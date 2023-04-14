#include "chunked_stream.h"

#include "character_io.h"

namespace coroserver {

ChunkedStream::ChunkedStream(std::shared_ptr<IStream> proxied, bool allow_read, bool allow_write)
:AbstractProxyStream(std::move(proxied))
{
    if (allow_read) {
        _reader = start_reader();
    } else {
        _eof_reached = true;
    }
    if (allow_write) {
        _writer = start_writer();
    } else {
        _eof_writen = true;
    }
}

cocls::future<std::string_view> ChunkedStream::read() {
    if (!_reader) return  cocls::future<std::string_view>::set_value();
    return _reader();
}

cocls::future<bool> ChunkedStream::write(std::string_view buffer) {
    if (!_writer) return cocls::future<bool>::set_value(true);
    if (buffer.empty()) return cocls::future<bool>::set_value(true);
    return _writer(buffer);
}

cocls::future<bool> ChunkedStream::write_eof() {
    return _writer(std::string_view());
}

cocls::generator<std::string_view> ChunkedStream::start_reader() {
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

static char * write_hex(std::size_t sz, char *ptr) {
    auto n = sz;
    char *c = ptr;
    ++ptr;
    while (n > 0xF) {
        n >>=4;
        ++ptr;
    }
    char *d = ptr;
    char digits[] = "0123456789abcdef";
    while (d != c) {
        d--;
        n = sz & 0xF;
        sz = sz >> 4;
        *d = digits[n];
    }
    return ptr;

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
    if (!_eof_reached && !_eof_writen) {
        _proxied->shutdown();
    }
}

cocls::generator<bool, std::string_view> ChunkedStream::start_writer() {
    std::string_view data = co_yield nullptr;
    std::vector<char> buff;
    while (!data.empty()) {
        std::size_t needsz = data.size()+20;
        if (buff.size() < needsz) {
            buff.resize(0);
            buff.resize(needsz);
        }
        char *ptr = buff.data();
        ptr = write_hex(data.size(), ptr);
        *ptr++='\r';
        *ptr++='\n';
        ptr = std::copy(data.begin(), data.end(), ptr);
        *ptr++='\r';
        *ptr++='\n';
        data = {buff.data(), std::size_t(ptr - buff.data())};
        data = co_yield co_await _proxied->write(data);
    }
    co_await _proxied->write("0\r\n\r\n");
    _eof_writen = true;
    while (true) {
        co_yield false;
    }
}



}
