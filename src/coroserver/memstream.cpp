#include "memstream.h"

namespace coroserver {

TimeoutSettings MemStream::get_timeouts() {
    return _tms;
}

PeerName MemStream::get_peer_name() const {
    return PeerName();
}



coro::future<std::string_view> MemStream::read() {
    std::string_view data = read_putback_buffer();
    _cntr.read+=data.size();
    return data;
}

std::string_view MemStream::read_nb() {
    return read_putback_buffer();
}

coro::future<bool> MemStream::write(std::string_view buffer) {
    if (_write_closed) return false;
    _cntr.write+=buffer.size();
    std::copy(buffer.begin(), buffer.end(), std::back_inserter(_output_buff));
    return true;
}

coro::future<bool> MemStream::write_eof() {
    bool x = std::exchange(_write_closed, true);
    return !x;
}

void MemStream::set_timeouts(const TimeoutSettings &tm) {
    _tms = tm;
}

Stream MemStream::create() {
    return Stream(std::make_shared<MemStream>());
}

Stream MemStream::create(std::string_view input) {
    return Stream(std::make_shared<MemStream>(input));
}

Stream MemStream::create(std::vector<char> input) {
    return Stream(std::make_shared<MemStream>(std::move(input)));
}

void MemStream::shutdown() {

}

std::string_view MemStream::get_output(Stream s) {
    auto device = s.getStreamDevice();
    MemStream *m = dynamic_cast<MemStream *>(device.get());
    if (m) {
        return m->get_output();
    } else {
        return {};
    }
}

std::vector<char>& MemStream::get_output_buff(Stream s) {
    auto device = s.getStreamDevice();
    MemStream *m = dynamic_cast<MemStream *>(device.get());
    if (m) {
        return m->get_output_buff();
    } else {
        throw std::bad_cast();
    }
}

void MemStream::clear_output(Stream s) {
    auto device = s.getStreamDevice();
    MemStream *m = dynamic_cast<MemStream *>(device.get());
    if (m) {
        m->clear_output();
    }
}

bool MemStream::is_read_timeout() const
{
    return false;
}

MemStream::Counters MemStream::get_counters() const noexcept {
    return _cntr;
}

}

