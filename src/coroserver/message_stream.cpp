#include "message_stream.h"

namespace coroserver {

MessageStream::MessageStream(const std::shared_ptr<IStream> &proxied, std::size_t max_msg_size)
        :AbstractProxyStream(std::move(proxied))
        ,_max_message_size(max_msg_size)
        ,_read_awt(*this)
        ,_write_awt(*this)
         {

}

coro::future<std::string_view> MessageStream::read() {
    return [&](auto promise) {
        auto pb = read_putback_buffer();
        if (!pb.empty()) {
            promise(pb);
            return;
        }

        _read_result  = std::move(promise);
        _read_awt << [&]{return _proxied->read();};
    };
}

Stream MessageStream::create(const Stream &target, std::size_t max_msg_size) {
    return Stream(std::make_shared<MessageStream>(target.getStreamDevice(), max_msg_size));
}

void MessageStream::encode_size(std::size_t sz, bool first) {
    if (sz) {
        encode_size(sz >> 7, false);
        unsigned char p = (sz & 0x7F) | (bool(!first)<<7);
        _write_msg_buffer.push_back(p);
    }
}

coro::future<bool> MessageStream::write(std::string_view buffer) {
    return [&](auto promise) {
        if (buffer.empty()) {
            promise(true);
            return;
        }
        _write_payload = buffer;
        _write_result = std::move(promise);
        _write_msg_buffer.clear();
        encode_size(buffer.size());
        _write_awt << [&]{return _proxied->write(_write_msg_buffer);};
    };
}

coro::future<bool> MessageStream::write_eof() {
    return [&](auto promise) {
        _write_payload = {};
        _write_result = std::move(promise);
        _write_msg_buffer.clear();
        _write_msg_buffer.push_back('\0');
        _write_awt << [&]{return _proxied->write(_write_msg_buffer);};
    };

}

coro::suspend_point<void> MessageStream::on_read(coro::future<std::string_view> &f) noexcept {
    try {
        std::string_view data = *f;
        for (std::size_t i = 0, cnt = data.size() ; i < cnt; ++i) {
            if (_reading_size) {
                unsigned char c= data[i];
                _read_size = (_read_size << 7) | (c & 0x7F);
                 if ((c & 0x80) == 0) {
                     _reading_size = false;
                     if (!_read_size || _max_message_size < _read_size) {
                         _proxied->put_back(data.substr(i+1));
                         return _read_result();
                     }
                     _read_msg_buffer.clear();
                     _read_msg_buffer.reserve(_read_size);
                 }
            } else {
                auto remain = std::min(i - cnt, _read_size);
                std::copy(data.data()+i, data.data()+i+remain,std::back_inserter(_read_msg_buffer));
                i+=remain-1;
                if (_read_msg_buffer.size() ==_read_size) {
                    _reading_size = true;
                    _read_size = 0;
                    _proxied->put_back(data.substr(i+1));
                    return _read_result(_read_msg_buffer.data(), _read_msg_buffer.size());
                }
                assert(_read_msg_buffer.size() <_read_size);
            }
        }

        _read_awt << [&]{return _proxied->read();};
        return {};

    } catch (...) {
        return _read_result(std::current_exception());
    }
}

coro::suspend_point<void> MessageStream::on_write(coro::future<bool> &f) noexcept {
    try {
        bool r = *f;
        auto p = std::exchange(_write_payload,{});
        if (r && !p.empty()) {
            _write_awt << [&]{return _proxied->write(p);};
            return {};
        }
        return _write_result(r);
    } catch (...) {
        return _write_result(std::current_exception());
    }
}

}
