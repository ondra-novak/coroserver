/*
 * stream.cpp
 *
 *  Created on: 25. 3. 2023
 *      Author: ondra
 */

#include "stream.h"

namespace coroserver {

class NullStream: public AbstractStreamWithMetadata {
public:
    NullStream():AbstractStreamWithMetadata({}) {}
    virtual cocls::future<std::string_view> read() override;
    virtual IStream::Counters get_counters() const noexcept override;
    virtual PeerName get_peer_name() const override;
    virtual cocls::future<bool> write(std::string_view buffer) override;
    virtual bool is_read_timeout() const override;
    virtual cocls::future<bool> write_eof() override;
    virtual cocls::suspend_point<void> shutdown() override;
};

Stream Stream::null_stream() {
    static Stream s(std::make_shared<NullStream>());
    return s;

}
cocls::future<std::string_view> NullStream::read() {
    return cocls::future<std::string_view>::set_value();
}

IStream::Counters NullStream::get_counters() const noexcept {
    return {};
}

PeerName NullStream::get_peer_name() const {
    return {};
}

cocls::future<bool> NullStream::write(std::string_view) {
    return cocls::future<bool>::set_value(false);
}

bool NullStream::is_read_timeout() const {
    return false;
}

cocls::future<bool> NullStream::write_eof() {
    return cocls::future<bool>::set_value(true);
}

cocls::suspend_point<void> NullStream::shutdown() {
    return {};
}

}
