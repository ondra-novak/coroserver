#include "memstream.h"

namespace coroserver {

TimeoutSettings MemStream::get_timeouts() {
    return _tms;
}

PeerName MemStream::get_source() const {
    return PeerName();
}

cocls::future<std::string_view> MemStream::read() {
    std::string_view data = read_putback_buffer();
    return cocls::future<std::string_view>::set_value(data);
}

std::string_view MemStream::read_nb() {
    return read_putback_buffer();
}

cocls::future<bool> MemStream::write(std::string_view buffer) {
    if (_write_closed) return cocls::future<bool>::set_value(false);
    std::copy(buffer.begin(), buffer.end(), std::back_inserter(_output_buff));
    return cocls::future<bool>::set_value(true);
}

cocls::future<bool> MemStream::write_eof() {
    bool x = std::exchange(_write_closed, true);
    return cocls::future<bool>::set_value(!x);
}

void MemStream::set_timeouts(const TimeoutSettings &tm) {
    _tms = tm;
}

void MemStream::shutdown() {
    //empty
}

bool MemStream::is_read_timeout() const {
    return false;
}

}
