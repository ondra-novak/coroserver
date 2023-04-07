#include "broadcaster.h"
#include <cocls/callback_awaiter.h>

namespace coroserver {



cocls::generator<std::string_view> BroadcastingStream::ListeningStream::reader_worker(cocls::signal<std::string_view>::emitter emt) {
    try {
        std::string_view data;
        while (true) {
            data = co_await emt;
            co_yield data;
        }
    } catch (const cocls::await_canceled_exception &) {
        //empty - will exit
    }
}

BroadcastingStream::ListeningStream::ListeningStream(PBroadcastingStream owner)
:_owner(std::move(owner))
{
    cocls::signal<std::string_view> sig(_owner->_coll);
    _reader = reader_worker(sig.get_emitter());
}


cocls::future<std::string_view> BroadcastingStream::ListeningStream::read() {
    auto buff = read_putback_buffer();
    if (!buff.empty() || _reader.done()) return cocls::future<std::string_view>::set_value(buff);
    return _reader();

}

std::string_view BroadcastingStream::ListeningStream::read_nb() {
    return read_putback_buffer();
}

cocls::future<bool> BroadcastingStream::ListeningStream::write(std::string_view data) {
    auto &buff = _owner->_write_buff;
    std::copy(data.begin(), data.end(), std::back_inserter(buff));
    _owner->_write_wait();
    return cocls::future<bool>::set_value(true);
}

cocls::future<bool> BroadcastingStream::ListeningStream::write_eof() {
    _owner->_write_wait();
    return cocls::future<bool>::set_value(true);
}

TimeoutSettings BroadcastingStream::ListeningStream::get_timeouts() {
    return {};
}

PeerName BroadcastingStream::ListeningStream::get_source() const {
    return {};
}

void BroadcastingStream::ListeningStream::set_timeouts(const TimeoutSettings &) {
}

void BroadcastingStream::ListeningStream::shutdown() {
    //empty;
}

bool BroadcastingStream::ListeningStream::is_read_timeout() const {
    return false;
}

PBroadcastingStream BroadcastingStream::create() {
    return std::make_shared<BroadcastingStream>();
}

coroserver::TimeoutSettings BroadcastingStream::get_timeouts() {
    return {};
}

coroserver::PeerName BroadcastingStream::get_source() const {
    return {};
}

std::string_view BroadcastingStream::read_nb() {
    auto buff = read_putback_buffer();
    if (!buff.empty()) return buff;
    std::swap(_read_buff, _write_buff);
    _write_buff.clear();
    return std::string_view(_read_buff.data(), _read_buff.size());
}

cocls::future<bool> BroadcastingStream::write(std::string_view buffer) {
    _coll(buffer);
    return cocls::future<bool>::set_value(true);
}

cocls::future<bool> BroadcastingStream::write_eof() {
    return cocls::future<bool>::set_value(true);
}

void BroadcastingStream::set_timeouts(const coroserver::TimeoutSettings &) {}

void BroadcastingStream::shutdown() {
    //
}

bool BroadcastingStream::is_read_timeout() const {return false;}

cocls::future<std::string_view> BroadcastingStream::read() {
    return [&](cocls::promise<std::string_view> prom) {
        std::string_view x = read_nb();
        if (!x.empty()) {
            prom(x);
        } else {
            cocls::callback_await<cocls::future<void> >(
                [this,prom = std::move(prom)](cocls::await_result<void> res) mutable {
                    try {
                        res.get();
                        std::string_view x = read_nb();
                        prom(x);
                    } catch (...) {
                        prom(std::current_exception());
                    }
            }, [this](cocls::promise<void> p2){
                _write_wait = std::move(p2);
                if (!_write_buff.empty()) _write_wait(true);
            });
        }
    };
}

}
