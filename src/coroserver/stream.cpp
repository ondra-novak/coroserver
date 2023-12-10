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
    virtual coro::future<std::string_view> read() override;
    virtual IStream::Counters get_counters() const noexcept override;
    virtual PeerName get_peer_name() const override;
    virtual coro::future<bool> write(std::string_view buffer) override;
    virtual bool is_read_timeout() const override;
    virtual coro::future<bool> write_eof() override;
    virtual void shutdown() override;
};

Stream Stream::null_stream() {
    static Stream s(std::make_shared<NullStream>());
    return s;

}
coro::future<std::string_view> NullStream::read() {
    return std::string_view();
}

IStream::Counters NullStream::get_counters() const noexcept {
    return {};
}

PeerName NullStream::get_peer_name() const {
    return {};
}

coro::future<bool> NullStream::write(std::string_view) {
    return false;
}

bool NullStream::is_read_timeout() const {
    return false;
}

coro::future<bool> NullStream::write_eof() {
    return true;
}

void NullStream::shutdown() {

}

}
