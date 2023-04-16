/*
 * limited_stream.cpp
 *
 *  Created on: 15. 4. 2023
 *      Author: ondra
 */

#include "limited_stream.h"

namespace coroserver {

coroserver::LimitedStream::LimitedStream(std::shared_ptr<IStream> proxied,
        std::size_t limit_read, std::size_t limit_write)
:AbstractProxyStream(std::move(proxied))
,_limit_read(limit_read)
,_limit_write(limit_write)
{

}

cocls::future<std::string_view> LimitedStream::read() {
    auto buff = read_putback_buffer();
    if (!buff.empty() || !_limit_read) return cocls::future<std::string_view>::set_value(buff);
    return [&](auto promise) {
        _awt.set_resume_fn([](cocls::awaiter *, void *ptr) noexcept ->cocls::suspend_point<void>{
            LimitedStream *me = reinterpret_cast<LimitedStream *>(ptr);
            std::string_view s = *me->_rdfut;
            std::string_view ret = s.substr(0,me->_limit_read);
            me->_proxied->put_back(s.substr(ret.size()));
            me->_limit_read -= ret.size();
            return me->_fltfut(ret);
        }, this);
        _fltfut = std::move(promise);
        _rdfut << [&]{return _proxied->read();};
        if (!_rdfut.subscribe(&_awt)) {
            _awt.resume();
        }
    };

}

cocls::future<bool> LimitedStream::write(std::string_view buffer) {
    if (buffer.size() > _limit_write) return cocls::future<bool>::set_value(false);
    _limit_write-=buffer.size();
    return _proxied->write(buffer);
}

cocls::future<bool> LimitedStream::write_eof() {
}

}



