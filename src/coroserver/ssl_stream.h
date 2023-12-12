/*
 * ssl_stream.h
 *
 *  Created on: 20. 11. 2022
 *      Author: ondra
 */

#ifndef SRC_USERVER_SSL_STREAM_H_
#define SRC_USERVER_SSL_STREAM_H_

#include "ssl_common.h"

#include "stream.h"
#include <openssl/ssl.h>
#include <functional>
#include <coro.h>
namespace coroserver {

namespace ssl {

using _Stream = Stream;

class Stream: public AbstractProxyStream {
public:
    Stream(_Stream target, Context ctx);


    Stream(const Stream &) = delete;
    Stream &operator=(const Stream &) = delete;
    ~Stream();

    virtual coro::future<std::string_view> read() override;
    virtual coro::future<bool> write(std::string_view data) override;
    virtual coro::future<bool> write_eof() override;

    static _Stream accept(_Stream s, Context ctx);
    static _Stream accept(_Stream s, Context ctx, const Certificate &server_cert);
    static _Stream connect(_Stream s, Context ctx);
    static _Stream connect(_Stream s, Context ctx, const std::string &hostname);
    static _Stream connect(_Stream s, Context ctx, const std::string &hostname, const Certificate &client_cert);

    ///Creates generator of SSL streams,
    /**
     * @param gen generator of unsecured streams
     * @param ctx SSL context
     * @param ssl_error (optional) function called when exception is thrown from the accept function (for logging)
     * @return
     */
    static coro::generator<_Stream> accept(coro::generator<_Stream> gen, Context ctx, std::function<void()> ssl_error = {});

protected:

    void connect_mode();
    void connect_mode(const std::string &hostname);
    void connect_mode(const std::string &hostname, const Certificate &client_cert);
    void accept_mode();
    void accept_mode(const Certificate &server_cert);
    std::recursive_mutex _mx;


    enum State {
        not_established,
        established,
        closing,
        closed
    };

    SSLObject _ssl;
    BIO *_read_data;
    BIO *_write_data;
    State _state = not_established;

    std::array<char, 16384> _rdbuff;
    std::string_view _wrbuff;
    std::vector<char> _encrypted_write_buffer;


    coro::promise<std::string_view> _read_result;
    coro::promise<bool> _write_result;

    coro::mutex _rdmx;
    coro::mutex _wrmx;
    coro::mutex _handshake;



//    coro::reusable_storage _rdstor;
//    coro::reusable_storage _wrstor;


    ///special result from run_ssl_io - operation complete, return retval
    static constexpr int _run_ssl_result_complete = 1;
    ///special result from run_ssl_io - repeat function call
    static constexpr int _run_ssl_result_retry = 2;

    enum class Op{
        read,
        write,
        establish_read,
        establish_write,
    };


  //  template<typename Ret, typename Fn>
  //  coro::with_allocator<coro::reusable_storage, coro::async<Ret> > run_ssl_io(coro::reusable_storage &, Fn fn, Ret failRet);

    void read_begin();
    void write_begin();
    void write_eof_begin();
    void begin_ssl();
    void establish_begin();
    bool flush_output(coro::mutex::target_type &target);

    template<std::invocable<> Fn>
    bool handle_ssl_error(int r, coro::mutex::target_type &target, Fn &&zero_fn);

    // contains target activated ater unlock of IO operation during reading
    coro::mutex::target_type _reader_unlock_target;
    // contains target activated ater unlock of IO operation during writing
    coro::mutex::target_type _writer_unlock_target;
    // contains target activated ater unlock of IO operation during handshake
    coro::mutex::target_type _handshake_unlock_target;
    // contains target activated ater unlock of IO operation during handshake
    coro::mutex::target_type _shutdown_unlock_target;

    // if async IO ends with exception, it is stored there
    std::exception_ptr _error_state;
    // if async IO ends with timeout, this flag is true
    bool _read_timeout = false;

    // stores ownership of a lock for reading IO
    coro::mutex::ownership _read_ownership;
    // stores target for reading IO
    coro::future<std::string_view>::target_type _read_fut_target;
    // stores future for reading IO
    coro::future<std::string_view> _read_fut;

    // stores ownership of a lock for writing IO
    coro::mutex::ownership _write_ownership;
    // stores target for writing IO
    coro::future<bool>::target_type _write_fut_target;
    // stores future for writing IO
    coro::future<bool> _write_fut;

    // stores ownership of a lock, while handshake is performed
    coro::mutex::ownership _handshake_ownership;


};


}

}




#endif /* SRC_USERVER_SSL_STREAM_H_ */
