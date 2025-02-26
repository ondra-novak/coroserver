#include "ssl_stream.h"
#include "ssl_error.h"
#include <openssl/ssl.h>

namespace coroserver {

SSLStream::SSLStream(TypeServer, Stream s,
        SSL_CTX *ctx, SetCertificateCallback cert_cb)
:StreamProxy(std::move(s))
,_cert_cb(std::move(cert_cb))
{
    common_init(ctx);
    SSL_set_cert_cb(_ssl, [](SSL *ssl, void *arg){
        SSLStream *me = reinterpret_cast<SSLStream *>(arg);
        const char *sni = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
        try {
            return me->_cert_cb(ssl, sni)?1:0;
        } catch (const std::exception &e) {
            ERR_put_error(ERR_LIB_SSL, 0, SSL_R_CERT_CB_ERROR, __FILE__, __LINE__);
            ERR_add_error_data(1, e.what());
            return 0; // Vrácení chyby do OpenSSL
        } catch (...) {
            ERR_put_error(ERR_LIB_SSL, 0, SSL_R_CERT_CB_ERROR, __FILE__, __LINE__);
            ERR_add_error_data(1, "Unknown exception in certificate callback");
            return 0;
        }
    }, this);
    SSL_accept(_ssl);
    _state = StreamState::opening;
    handle_error(SSL_do_handshake(_ssl));

}

SSLStream::SSLStream(TypeClient, Stream s,
        SSL_CTX *ctx, std::string sni_host)
:StreamProxy(std::move(s))
{
    common_init(ctx);
    if (!sni_host.empty()) {
          if (SSL_set_tlsext_host_name(_ssl, sni_host.c_str()) != 1) {
                  SSL_free(_ssl);
                  throw SSLException("Failed to set SNI");
          }
    }
    SSL_connect(_ssl);
    _state = StreamState::opening;
    handle_error(SSL_do_handshake(_ssl));

}

SSLStream::~SSLStream() {
    SSL_free(_ssl); // This will free the associated BIOs as well.
}

void SSLStream::common_init(SSL_CTX *ctx) {

    _ssl = SSL_new(ctx);
    if (!_ssl) {
        throw SSLException("Failed to create SSL object");
    }

    // Create memory BIOs for I/O.
    _rbio = BIO_new(BIO_s_mem());
    _wbio = BIO_new(BIO_s_mem());
    _wbio2 = BIO_new(BIO_s_mem());

    if (!_rbio || !_wbio) {
        SSL_free(_ssl);
        throw SSLException("Failed to create BIOs");
    }

    // Set our memory BIOs into the SSL object.
    // Note: SSL_set_bio() takes ownership of the BIOs.
    SSL_set_bio(_ssl, _rbio, _wbio);

    _state = StreamState::opening;
}

coroserver::StreamState SSLStream::get_state() const {
    auto state = _s.get_state();
    if (state == StreamState::active) state = _state;
    return state;
}

awaitable<std::string_view> SSLStream::receive() {
    if (!_putback_buffer.empty() || _state == StreamState::closed) {
        return std::exchange(_putback_buffer, {});
    }
    while (true) {
        std::unique_lock lk(_mx);
        if (_state == StreamState::closed) return std::string_view();
        if (_state == StreamState::opening) {
            return [this, lk = std::move(lk)](RecvResult res) {
                this->_recv_awaiting = std::move(res);
                if (!_handshake_running) {
                    run_handshake();
                }
            };
        }
        auto r = read_ssl_nb();
        if (!r.empty()) return r;
        if (_state == StreamState::closed) return r;
        if (_state == StreamState::opening) continue;
        return [this, lk = std::move(lk)](RecvResult res) {
            this->_recv_awaiting = std::move(res);
            _async_receive_cb.await(_s.receive(),this);
        };
    }

}

awaitable<bool> SSLStream::send(std::string_view data) {
    while (true) {
        std::unique_lock lk(_mx);
        if (_state == StreamState::closed || _state == StreamState::closing) {
            return false;
        }
        if (_state == StreamState::opening) {
            this->_send_awaiting_data = data;
            return [this, lk = std::move(lk)](SendResult res) {
                this->_send_awaiting = std::move(res);
                if (!_handshake_running) {
                    run_handshake();
                }
            };
        }
        if (data.empty()) return _s.send(data); //just sync

        bool st = handle_error(SSL_write(_ssl, data.data(), data.size()));
        if (st) {
            std::string_view  out_data = get_output_data();
            if (!out_data.empty()) return _s.send(out_data);
            return true;
        } else {
            //NOTE: false is returned when write wants to read
            //which means renegotiation was started
            if (_state == StreamState::active) {
                //so continue in handshake
                _state = StreamState::opening;
            }
            continue;
        }

    }
}

bool SSLStream::handle_error(int retval) {
    int e = SSL_get_error(_ssl, retval);
    switch (e) {
        case SSL_ERROR_NONE: return true;
        case SSL_ERROR_ZERO_RETURN:
            _state = StreamState::closed;
            return true;
        case SSL_ERROR_WANT_READ:
        case SSL_ERROR_WANT_WRITE:
            return false;
        case SSL_ERROR_SYSCALL:
            _state = StreamState::closed;
            throw std::system_error(errno, std::system_category());
        case SSL_ERROR_SSL:
            _state = StreamState::closed;
            throw SSLException();
        default:
            _state = StreamState::closed;
            throw std::runtime_error("Unexcpected SSL 'want' status");
    }
}

std::string_view SSLStream::read_ssl_nb() {
    void *dummy;
    size_t sz = BIO_get_mem_data(_rbio, &dummy);
    sz = std::min<std::size_t>(sz, 1024);
    if (sz > _decrypt_buffer.size()) {
        _decrypt_buffer.clear();
        _decrypt_buffer.resize(sz);
    }
    int l = SSL_read(_ssl, _decrypt_buffer.data(), _decrypt_buffer.size());
    if (handle_error(l)) {
        return {_decrypt_buffer.data(),static_cast<std::size_t>(l)};
    } else {
        return {};
    }
}

std::string_view SSLStream::get_output_data() {
    BUF_MEM *buf1;
    BUF_MEM *buf2;
    BIO_reset(_wbio2);
    BIO_get_mem_ptr(_wbio,&buf1);
    BIO_get_mem_ptr(_wbio2,&buf2);
    BIO_set_mem_buf(_wbio,buf2, BIO_CLOSE);
    BIO_set_mem_buf(_wbio2,buf1, BIO_CLOSE);
    return {reinterpret_cast<const char *>(buf1->data),static_cast<std::size_t>(buf1->length)};
}

}
