#pragma once
#include <coroserver/stream.hpp>
#include <functional>

typedef struct bio_st BIO;
typedef struct ssl_st SSL;
typedef struct ssl_ctx_st SSL_CTX;



namespace coroserver {



class SSLStream: public StreamProxy {
public:

    enum TypeClient {client};
    enum TypeServer {server};

    ///Called to select certificate
    /** Function is mirror of SSL_CTX_set_cert_cb, it is called during handshake to
     * choose certificate. The function must set certificate and private key on SSL
     * object and return true. Return value false means error
     *
     * @param 1 pointer to SSL object
     * @param 2 SNI string
     */
    using SetCertificateCallback = std::function<bool(SSL *, std::string_view)>;


    SSLStream(TypeServer, Stream s, SSL_CTX *ctx, SetCertificateCallback cert_cb = {});
    SSLStream(TypeClient, Stream s, SSL_CTX *ctx, std::string sni_host);

    SSLStream(const SSLStream &) = delete;
    SSLStream &operator=(const SSLStream &) = delete;

    virtual coroserver::StreamState get_state() const override;
    virtual coro::awaitable<std::string_view> read() override;
    virtual void put_back(std::string_view s) override;
    virtual coro::awaitable<bool> write(std::string_view data) override;
    virtual coro::awaitable<bool> close() override;


protected:
    struct TwoCoros {
        coro::prepared_coro first = {};
        coro::prepared_coro second = {};
    };

    using RecvResult = coro::awaitable<std::string_view>::result;
    using SendResult = coro::awaitable<bool>::result;

    struct SSLDeleter{void operator()(SSL *ssl) const;};
    struct BIODeleter{void operator()(BIO *bio) const;};

    std::mutex _mx;
    std::unique_ptr<SSL, SSLDeleter>_ssl = nullptr;
    BIO *_rbio = nullptr;
    BIO *_wbio = nullptr;
    std::unique_ptr<BIO, BIODeleter>_wbio2 = nullptr;
    StreamState _state = {};
    SetCertificateCallback _cert_cb;
    std::vector<char> _decrypt_buffer;
    bool _handshake_running;

    //recv side
    RecvResult _recv_awaiting;
    std::string_view _putback_buffer;

    //write side
    SendResult _write_awaiting;
    std::string_view _write_awaiting_data;
    bool _req_close = false;


    TwoCoros fail_io(std::exception_ptr e);
    TwoCoros fail_io();
    TwoCoros finish_handshake();


    TwoCoros async_process_read(coro::awaitable<std::string_view> &awt);
    TwoCoros async_process_write(coro::awaitable<bool> &awt);

    coro::awaiting_callback<coro::awaitable<std::string_view>,SSLStream *> _async_read_cb;
    coro::awaiting_callback<coro::awaitable<bool>,SSLStream *> _async_write_cb;

    void common_init(SSL_CTX *ctx);
    bool handle_error(int retval);

    std::string_view read_ssl_nb();
    coro::prepared_coro run_handshake();

    template<typename Res>
    coro::prepared_coro run_handshake_except(Res &res);

    std::string_view get_output_data();


};

}
