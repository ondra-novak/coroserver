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
{
    coro::target_member_fn_activation<&LimitedStream::join_read>(_read_fut_target, this);

}

coro::future<std::string_view> LimitedStream::read() {
    auto buff = read_putback_buffer();
    if (!buff.empty() || !_limit_read) return buff;
    return [&](auto promise) {
        _read_result = std::move(promise);
        _read_fut << [&]{return _proxied->read();}; //continue by join
        _read_fut.register_target(_read_fut_target);
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

void LimitedStream::join_read(coro::future<std::string_view> *fut) noexcept {
    try {
        std::string_view data = *fut;
        auto ret = data.substr(0, _limit_read);
        _proxied->put_back(data.substr(ret.size()));
        _limit_read -= ret.size();
        _read_result(ret);
    } catch (...) {
        _read_result.reject();
    }

}

coro::future<bool> LimitedStream::write(std::string_view buffer) {
    if (buffer.empty()) return true;
    auto b = buffer.substr(0, _limit_write);
    if (b.empty()) return false;
    _limit_write-=b.size();
    return _proxied->write(b);
}

coro::future<bool> LimitedStream::write_eof() {
    if (_limit_write) {
        return ([&]()->coro::async<bool> {
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
        return true;
    }
}


}



