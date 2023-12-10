/*
 * memstream.h
 *
 *  Created on: 26. 3. 2023
 *      Author: ondra
 */

#ifndef SRC_COROSERVER_MEMSTREAM_H_
#define SRC_COROSERVER_MEMSTREAM_H_
#include "stream.h"

#include <memory>
#include <string_view>
#include <vector>

namespace coroserver {

class MemStream;

using PMemStream=std::shared_ptr<MemStream>;

class MemStream:public AbstractStream {
public:
    MemStream()=default;
    MemStream(std::string_view input) {
        AbstractStream::put_back(input);
    }
    MemStream(std::vector<char> &&input)
        :_input_buff(std::move(input)) {
        AbstractStream::put_back(std::string_view(_input_buff.data(), _input_buff.size()));
    }


    std::string_view get_output() const {
        return std::string_view(_output_buff.data(),_output_buff.size());
    }

    std::vector<char>& get_output_buff() {
        return _output_buff;
    }


    void clear_output() {
        _output_buff.clear();
        _write_closed = false;
    }

    static std::string_view get_output(Stream s);
    static std::vector<char>& get_output_buff(Stream s);
    static void clear_output(Stream s);

    static Stream create();
    static Stream create(std::string_view input);
    static Stream create(std::vector<char> input);

    virtual TimeoutSettings get_timeouts() override;
    virtual PeerName get_peer_name() const override;
    virtual coro::future<std::string_view> read() override;
    virtual std::string_view read_nb() override;
    virtual coro::future<bool> write(std::string_view buffer) override;
    virtual coro::future<bool> write_eof() override;
    virtual void set_timeouts(const TimeoutSettings &tm) override;
    virtual coro::suspend_point<void> shutdown() override;
    virtual bool is_read_timeout() const override;
    virtual Counters get_counters() const noexcept override;



protected:
    Counters _cntr;
    std::vector<char> _input_buff;
    std::vector<char> _output_buff;
    TimeoutSettings _tms;
    bool _write_closed = false;


};



}



#endif /* SRC_COROSERVER_MEMSTREAM_H_ */
