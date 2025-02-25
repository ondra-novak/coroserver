#include "ssl_error.h"
#include "ssl_engine.h"
#include <openssl/err.h>
#include <cstring>
#include <system_error>

SSLEngine::SSLEngine(TypeClient, SSL_CTX *ctx, std::string host_name) {
    common_init(ctx);
    if (!host_name.empty()) {
          if (SSL_set_tlsext_host_name(_ssl, host_name.c_str()) != 1) {
                  SSL_free(_ssl);
                  throw std::runtime_error("Failed to set SNI");
          }
    }
    SSL_connect(_ssl);
    _state = handle_error(SSL_do_handshake(_ssl))?StreamState::active:StreamState::opening;
}

SSLEngine::SSLEngine(TypeServer, SSL_CTX *ctx) {
    common_init(ctx);
    SSL_accept(_ssl);
    _state = handle_error(SSL_do_handshake(_ssl))?StreamState::active:StreamState::opening;
}

bool SSLEngine::handle_error(int ret) {
    if (ret <= 0) {
        int err = SSL_get_error(_ssl, ret);
        if (err == SSL_ERROR_ZERO_RETURN) {
            _state = StreamState::closed;
        } else if (err == SSL_ERROR_SSL) {
            _state = StreamState::closed;
            throw SSLException();
        } else if (err == SSL_ERROR_SYSCALL) {
            _state = StreamState::closed;
            throw std::system_error(errno, std::system_category());
        }
        return false;
    }
    return true;
}

void SSLEngine::common_init(SSL_CTX *ctx) {

    _ssl = SSL_new(ctx);
    if (!_ssl) {
        throw std::runtime_error("Failed to create SSL object");
    }

    // Create memory BIOs for I/O.
    _rbio = BIO_new(BIO_s_mem());
    _wbio = BIO_new(BIO_s_mem());
    if (!_rbio || !_wbio) {
        SSL_free(_ssl);
        throw std::runtime_error("Failed to create BIOs");
    }

    // Set our memory BIOs into the SSL object.
    // Note: SSL_set_bio() takes ownership of the BIOs.
    SSL_set_bio(_ssl, _rbio, _wbio);

    // Optional: set auto-retry mode.
    SSL_set_mode(_ssl, SSL_MODE_AUTO_RETRY);

    _state = StreamState::opening;
}

SSLEngine::~SSLEngine() {
    if (_ssl) {
        SSL_free(_ssl); // This will free the associated BIOs as well.
    }
}

void SSLEngine::close() {
    if (_state == StreamState::active) {
        clear_output();;
        SSL_shutdown(_ssl);
    }
}

void SSLEngine::clear_output() {
    if (_clear_output) {
        BIO_reset(_wbio);
    }
}

StreamState SSLEngine::data_exchange(std::string_view &data) {
    if (_clear_output) BIO_reset(_wbio);
    if (!data.empty()) {
        BIO_write(_rbio, data.data(), data.size());
    }
    if (_state == StreamState::opening) {
        if (handle_error(SSL_do_handshake(_ssl))) {
            _state = StreamState::active;
        }
    }
    void *p;
    std::size_t sz = BIO_get_mem_ptr(_wbio, &p);
    data = std::string_view(reinterpret_cast<const char *>(p), sz);
    _clear_output = true;
    return _state;
}

StreamState SSLEngine::encrypt(std::string_view data) {
    clear_output();
    if (_state == StreamState::active) {
        if (!handle_error(SSL_write(_ssl, data.data(), data.size()))) {
            if (_state == StreamState::active) {
                _state == StreamState::opening;
            }
        }
    }
    return _state;
}

StreamState SSLEngine::decrypt(std::string_view &data) {
    clear_output();
    data = {};
    if (_state == StreamState::opening) return _state;
    void *_dummy;
    std::size_t sz = BIO_get_mem_data(_rbio, &_dummy);;
    sz = std::min<std::size_t>(sz, 2048);
    if (sz > _decrypt_buffer.size()) _decrypt_buffer.resize(sz);
    int n = SSL_read(_ssl, _decrypt_buffer.data(), _decrypt_buffer.size());
    if (n < 0) {
        handle_error(n);
        n = 0;
    }
    data = std::string_view(_decrypt_buffer.data(), _decrypt_buffer.size());
    return _state;
}
