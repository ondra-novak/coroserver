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
#include <cocls/mutex.h>
#include <cocls/coro_storage.h>
#include <cocls/generator.h>
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
    static coro::generator<_Stream> accept(coro::generator<_Stream> gen, Context ctx, coro::function<void()> ssl_error = {});

protected:

    void connect_mode();
    void connect_mode(const std::string &hostname);
    void connect_mode(const std::string &hostname, const Certificate &client_cert);
    void accept_mode();
    void accept_mode(const Certificate &server_cert);
    std::mutex _mx;


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
    std::vector<char> _wrbuff;

    coro::mutex _rdmx;
    coro::mutex _wrmx;

    coro::reusable_storage _rdstor;
    coro::reusable_storage _wrstor;


    ///special result from run_ssl_io - operation complete, return retval
    static constexpr int _run_ssl_result_complete = 1;
    ///special result from run_ssl_io - repeat function call
    static constexpr int _run_ssl_result_retry = 2;



    template<typename Ret, typename Fn>
    coro::with_allocator<coro::reusable_storage, coro::async<Ret> > run_ssl_io(coro::reusable_storage &, Fn fn, Ret failRet);

};


}

}




#endif /* SRC_USERVER_SSL_STREAM_H_ */
