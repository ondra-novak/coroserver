/*
 * limited_stream.cpp
 *
 *  Created on: 15. 4. 2023
 *      Author: ondra
 */

#include "limited_stream.h"

#include "character_io.h"
namespace coroserver {

coroserver::LimitedStream::LimitedStream(std::shared_ptr<IStream> proxied,
        std::size_t limit_read, std::size_t limit_write)
:AbstractProxyStream(std::move(proxied))
,_limit_read(limit_read)
,_limit_write(limit_write)
,_read_awt(*this)
{

}

cocls::future<std::string_view> LimitedStream::read() {
    auto buff = read_putback_buffer();
    if (!buff.empty() || !_limit_read) return cocls::future<std::string_view>::set_value(buff);
    return [&](auto promise) {
        _read_result = std::move(promise);
        _read_awt << [&]{return _proxied->read();}; //continue by join
    };
}

Stream LimitedStream::read(Stream target, std::size_t limit_read) {
    return Stream(std::make_shared<LimitedStream>(target.getStreamDevice(), limit_read,0));
}

Stream LimitedStream::write(Stream target, std::size_t limit_write) {
    return Stream(std::make_shared<LimitedStream>(target.getStreamDevice(), 0,limit_write));
}

Stream LimitedStream::read_and_write(Stream target, std::size_t limit_read,
        std::size_t limit_write) {
    return Stream(std::make_shared<LimitedStream>(target.getStreamDevice(), limit_read,limit_write));
}

LimitedStream::~LimitedStream() {
    if (_limit_read || _limit_write) _proxied->shutdown();
}

cocls::suspend_point<void> LimitedStream::join_read(cocls::future<std::string_view> &fut) noexcept {
    try {
        std::string_view data = *fut;
        auto ret = data.substr(0, _limit_read);
        _proxied->put_back(data.substr(ret.size()));
        _limit_read -= ret.size();
        return _read_result(ret);
    } catch (...) {
        return _read_result(std::current_exception());
    }

}

cocls::future<bool> LimitedStream::write(std::string_view buffer) {
    if (buffer.empty()) return cocls::future<bool>::set_value(true);
    auto b = buffer.substr(0, _limit_write);
    if (b.empty()) return cocls::future<bool>::set_value(false);
    _limit_write-=b.size();
    return _proxied->write(b);
}

cocls::future<bool> LimitedStream::write_eof() {
    if (_limit_write) {
        return ([&]()->cocls::async<bool> {
            CharacterWriter<Stream> wr(_proxied);
            bool ret;
            while (_limit_write) {
                ret = co_await wr(0);
                if (!ret) co_return ret;
                --_limit_write;
            }
            ret = co_await wr.flush();
            co_return ret;
        })();
    } else {
        return cocls::future<bool>::set_value(true);
    }
}


}



