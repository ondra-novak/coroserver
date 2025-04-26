#pragma once

#include "stream.hpp"
#include "context.hpp"

namespace coroserver {


class NullStream : public IStream {
public:
    virtual Context get_context() const override {return {nullptr};};
    virtual StreamState get_state() const override {return StreamState::closed;}
    virtual IOTimeout get_timeouts() const override {return {};}
    virtual IStream::Counters get_counters() const override {return {};}
    virtual awaitable<std::string_view> read() override {
        return std::exchange(_buff,{});
    }
    virtual awaitable<bool> write(std::string_view ) override {return false;}
    virtual awaitable<bool> close() override {return false;}
    virtual void shutdown() override {}
    virtual void set_timeouts(IOTimeout ) override {}
    virtual void put_back(std::string_view s) override {_buff = s;}


    ///creates stream which contains nothing
    /** @note NullStream doesn't contain a valid Context */
    static Stream create() {
        static NullStream ns;
        return Stream(std::shared_ptr<IStream>(&ns, [](auto){}));
    }
protected:
    std::string_view _buff;

};



}
