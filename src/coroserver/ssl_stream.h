#pragma once
#include "stream.h"
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
    ~SSLStream();

    SSLStream(const SSLStream &) = delete;
    SSLStream &operator=(const SSLStream &) = delete;

    virtual coroserver::StreamState get_state() const override;
    virtual awaitable<std::string_view> receive() override;
    virtual void put_back(std::string_view s) override;
    virtual awaitable<bool> send(std::string_view data) override;
    virtual awaitable<bool> close() override;

protected:

    using RecvResult = awaitable<std::string_view>::result;
    using SendResult = awaitable<bool>::result;

    std::mutex _mx;
    SSL *_ssl = nullptr;
    BIO *_rbio = nullptr;
    BIO *_wbio = nullptr;
    BIO *_wbio2 = nullptr;
    StreamState _state = {};
    SetCertificateCallback _cert_cb;
    std::vector<char> _decrypt_buffer;
    bool _handshake_running;

    //recv side
    RecvResult _recv_awaiting;
    std::string_view _putback_buffer;

    //send side
    SendResult _send_awaiting;
    std::string_view _send_awaiting_data;


    prepared_coro async_process_receive(awaitable<std::string_view> &awt);
    prepared_coro async_process_send(awaitable<bool> &awt);

    await_member_callback<std::string_view, SSLStream *,
                &SSLStream::async_process_receive> _async_receive_cb;
    await_member_callback<bool, SSLStream *,
                &SSLStream::async_process_send> _async_send_cb;



    void common_init(SSL_CTX *ctx);
    bool handle_error(int retval);

    std::string_view read_ssl_nb();
    void run_handshake();
    std::string_view get_output_data();


};

}
