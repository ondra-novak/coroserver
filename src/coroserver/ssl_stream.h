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

    ///when EOF is detected without proper shutdown, this can be reported as error
    /**
     * default is false
     *
     * When this value is true, then EOF state is passed to SSL library and can
     * be reported as error "ssl3_read_n:unexpected eof while reading". The
     * only valid way to send EOF through the SSL connection is to
     * shutdown session (SSL_Shutdown)
     *
     * When this value is false, closed connection withou SSL_Shutdown is
     * also considered as valid state and EOF is reported to the frontend
     *
     */
    static bool eof_without_shudown_is_error;

protected:

    void connect_mode();
    void connect_mode(const std::string &hostname);
    void connect_mode(const std::string &hostname, const Certificate &client_cert);
    void accept_mode();
    void accept_mode(const Certificate &server_cert);
    std::mutex _mx;


    enum class State {
        not_established,
        established,
        closing,
        closed
    };

    enum class Action {
        no_action,
        eof,
        read,
        write,
    };

    SSLObject _ssl;
    BIO *_read_data;
    BIO *_write_data;
    State _state = State::not_established;

    std::vector<char> _read_buffer;
    std::size_t _read_buffer_size = 1024;

    std::string_view _wrbuff;
    std::vector<char> _encrypted_write_buffer;


    template<typename RetVal>
    coro::generator<RetVal> io_coroutine();
    Action determine_ssl_state(int r);
    void post_ssl_read(std::string_view buffer);
    bool post_ssl_write(bool st);
    coro::async<bool> write_eof_coro();
    coro::future<bool> send_encrypted();
    coro::future<std::string_view> read_encrypted();


    coro::promise<std::string_view> _read_result;
    coro::promise<bool> _write_result;

    coro::mutex _rdmx;
    coro::mutex _wrmx;
    coro::mutex _handshake;



    coro::reusable_allocator _rdstor;
    coro::reusable_allocator _wrstor;


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






};


}

}




#endif /* SRC_USERVER_SSL_STREAM_H_ */
