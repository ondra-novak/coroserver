/*
 * broadcaster.h
 *
 *  Created on: 26. 3. 2023
 *      Author: ondra
 */

#ifndef SRC_COROSERVER_BROADCASTER_H_
#define SRC_COROSERVER_BROADCASTER_H_
#include "stream.h"

#include <cocls/signal.h>
#include <cocls/future.h>
#include <cocls/generator.h>

namespace coroserver {


class BroadcastingStream;
using PBroadcastingStream = std::shared_ptr<BroadcastingStream>;

///Broadcasting stream is stream act like a pipe. Data send to this stream are broadcasted to listeners
/**
 * @note The whole implementation expect synchronous access from perspective of
 * threads with benefit for coroutines. You should avoid to access from different
 * threads.
 *
 * Listeners can call create_listener and retrieve Stream, which can be read.
 * Using co_await on read, it can asynchronously wait for incomming data. Otherside
 * of this virtual pipe is writer, which can be obtained by the function create_writer()
 *
 * Writing anything to the writer wakes up all listeners and they can read content
 * written to the object. This is done synchronously by waking up one listener
 * after other. Each listener should avoid to suspend on anything, which can
 * transfer execution to a different thread, which can cause, that next broadcast
 * will be missed from them.
 *
 *
 * Listeners can also write, all writes are collected and can be read by broacastding
 * stream. Note writing is not MT safe in context of listeners, so only
 * one listener can write at time.
 *
 *
 */
class BroadcastingStream: public AbstractStream, public std::enable_shared_from_this<BroadcastingStream> {
public:

    class ListeningStream: public AbstractStream {
    public:
        ListeningStream(PBroadcastingStream owner);

        virtual cocls::future<std::string_view> read() override;
        virtual std::string_view read_nb() override;
        virtual cocls::future<bool> write(std::string_view buffer) override;
        virtual cocls::future<bool> write_eof() override;
        virtual coroserver::TimeoutSettings get_timeouts() override;
        virtual coroserver::PeerName get_source() const override;
        virtual void set_timeouts(const coroserver::TimeoutSettings &tm) override;
        virtual void shutdown() override;
        virtual bool is_read_timeout() const override;

    protected:
        PBroadcastingStream _owner;
        cocls::generator<std::string_view> _reader;
        cocls::generator<std::string_view> reader_worker(cocls::signal<std::string_view>::emitter emt);
    };


    BroadcastingStream()
        :_coll(cocls::signal<std::string_view>().get_collector()) {}


    Stream create_listener() {
        return Stream(std::make_shared<ListeningStream>(shared_from_this()));
    }

    Stream create_writer() {
        return Stream(shared_from_this());
    }


    static PBroadcastingStream create();

protected:

    virtual coroserver::TimeoutSettings get_timeouts() override;
    virtual coroserver::PeerName get_source() const override;
    virtual std::string_view read_nb() override;
    virtual cocls::future<bool> write(std::string_view buffer) override;
    virtual cocls::future<bool> write_eof() override;
    virtual void set_timeouts(const coroserver::TimeoutSettings &tm) override;
    virtual void shutdown() override;
    virtual bool is_read_timeout() const override;
    virtual cocls::future<std::string_view> read() override;


    cocls::signal<std::string_view>::collector _coll;
    std::vector<char> _write_buff;
    std::vector<char> _read_buff;
    cocls::promise<void> _write_wait;



};



}




#endif /* SRC_COROSERVER_BROADCASTER_H_ */
