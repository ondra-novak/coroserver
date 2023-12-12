#include "ssl_stream.h"

 #include <openssl/bio.h>
#include <openssl/x509_vfy.h>


namespace coroserver {

namespace ssl {


Stream::Stream(_Stream target, Context ctx):AbstractProxyStream(target.getStreamDevice()) {
    _ssl = SSL_new(ctx);
    _read_data = BIO_new(BIO_s_mem());
    _write_data = BIO_new(BIO_s_mem());

    BIO_set_mem_eof_return(_read_data, -1);
    BIO_set_mem_eof_return(_write_data, -1);
    SSL_set_bio(_ssl, _read_data, _write_data);

    coro::target_simple_activation(_reader_unlock_target, [&](coro::mutex::ownership){
        read_begin();
    });
    coro::target_simple_activation(_writer_unlock_target, [&](coro::mutex::ownership){
        write_begin();
    });
    coro::target_simple_activation(_handshake_unlock_target, [&](coro::mutex::ownership){
        establish_begin();
    });
    coro::target_simple_activation(_read_fut_target, [&](coro::future<std::string_view> *f){
        try {
            std::lock_guard lk(_mx);
            std::string_view data = *f;
            if (data.empty()) {
                if (_proxied->is_read_timeout()) _read_timeout = true;
                else BIO_set_mem_eof_return(_read_data,0);
            } else {
                BIO_write(_read_data, data.data(), data.length());
            }

        } catch (...) {
            _state = closed;
            _error_state = std::current_exception();
        }
        _read_ownership.reset();
    });
    coro::target_simple_activation(_write_fut_target, [&](coro::future<bool> *f){
        try {
            std::lock_guard lk(_mx);
            bool b = *f;
            if (!b) {
                _state = closed;
            }
        } catch (...) {
            _state = closed;
            _error_state = std::current_exception();
        }
        _write_ownership.reset();
    });



}

coro::future<std::string_view> Stream::read() {
    std::string_view tmp = AbstractStream::read_putback_buffer();
    if (!tmp.empty() || _state == closed) return tmp;
    return [&](auto promise) {
        _read_result = std::move(promise);
        read_begin();
    };
}

bool Stream::flush_output(coro::mutex::target_type &target) {
    char *buff;
    long sz = BIO_get_mem_data(_write_data,&buff); // @suppress("C-Style cast instead of C++ cast")

    if (sz) {

        auto own = _wrmx.try_lock();
        if (own) {
            _write_ownership = std::move(own);
            _encrypted_write_buffer.resize(sz);
            std::copy(buff, buff+sz, _encrypted_write_buffer.begin());
            BIO_reset(_write_data);
            std::string_view bw(_encrypted_write_buffer.data(), _encrypted_write_buffer.size());
            _write_fut << [&]{return _proxied->write(bw);};
            _write_fut.register_target(_write_fut_target);
        } else if (_wrmx.register_target_async(target) == nullptr) {
            return true;
        }
    }
    return false;

}

template<std::invocable<> Fn>
bool Stream::handle_ssl_error(int r, coro::mutex::target_type &target, Fn &&zero_fn) {


    if (flush_output(target)) return true;

    int ssl_state = SSL_get_error(_ssl, r);
    switch (ssl_state) {

        case SSL_ERROR_ZERO_RETURN: {
            zero_fn();
            return true;
        }
        case SSL_ERROR_WANT_READ: {
            auto own = _rdmx.try_lock();
            if (own) {
                _read_ownership = std::move(own);
                _read_timeout = false;
                _read_fut << [&]{return _proxied->read();};
                _read_fut.register_target(_read_fut_target);
            }
            if (_rdmx.register_target_async(target) == nullptr) {
                return true;
            }
            break;
        }
        case SSL_ERROR_SYSCALL:
            throw std::system_error(errno, std::system_category());
        default:
            throw SSLError();
    }
    return false;

}


void Stream::read_begin() {
    coro::promise<std::string_view>::pending_notify ntf;
    std::lock_guard lk(_mx);
    try {
        if (_error_state) std::rethrow_exception(_error_state);
        if (_state == not_established) {
            begin_ssl();
            if (_handshake.register_target_async(_reader_unlock_target) == nullptr) return;
        }
        while (true) {
            if (_state == closed || _read_timeout) {
                ntf = _read_result();
                break;
            }
            int r = SSL_read(_ssl, _rdbuff.data(), _rdbuff.size());
            if (r > 0) {
                ntf = _read_result(_rdbuff.data(), r);
                break;
            } else {
                if (handle_ssl_error(r, _reader_unlock_target, [&]{
                        _state = closed;
                        ntf = _read_result();

                })) return;
            }
        }
    } catch (...) {
        _state = closed;
        ntf = _read_result.reject();
    }
}

coro::future<bool> Stream::write(std::string_view data) {
    return [&](auto promise) {
        _wrbuff = data;
        _write_result = std::move(promise);
        write_begin();
    };

}

void Stream::write_begin() {
    coro::promise<bool>::pending_notify ntf;
    std::lock_guard lk(_mx);
    try {
        if (_error_state) std::rethrow_exception(_error_state);
        if (_state == not_established) {
            begin_ssl();
            if (_handshake.register_target_async(_writer_unlock_target) == nullptr) return;
        }
        while (true) {
            if (_state == closing || _state == closed) {
                ntf = _write_result(false);
                break;
            }
            auto wrsz = _wrbuff.size();
            if (wrsz == 0) {
                ntf = _write_result(true);
                break;
            }
            int r = SSL_write(_ssl, _wrbuff.data(), wrsz);
            if (r > 0) {
                _wrbuff = _wrbuff.substr(r);
                if (flush_output(_writer_unlock_target)) return;
                write_begin();
            } else {
                if (handle_ssl_error(r, _writer_unlock_target, [&]{
                    _state = closed;
                    ntf = _write_result(false);
                })) return;
            }
        }
    } catch (...) {
        _state = closed;
        ntf = _write_result.reject();
    }
}

void Stream::begin_ssl() {
    auto own = _handshake.try_lock();
    if (!own) return;
    _handshake_ownership = std::move(own);
    establish_begin();
}

void Stream::establish_begin() {
    std::lock_guard lk(_mx);
    try {
        while (true) {
            if (_state != not_established) {
                _handshake_ownership.reset();
                break;
            }
            auto r = SSL_do_handshake(_ssl);
            if (r > 0) {
                _state = established;
                _handshake_ownership.reset();
                break;
            } else {
                if (handle_ssl_error(r, _handshake_unlock_target, [&]{
                    _state = closed;
                    _handshake_ownership.reset();
                })) return;
            }
        }
    } catch (...) {
        _state = closed;
        _error_state = std::current_exception();
    }

}


coro::future<bool> Stream::write_eof() {
    return [&](auto promise) {
        _write_result = std::move(promise);
        write_eof_begin();
    };
}

void Stream::write_eof_begin() {
    coro::promise<bool>::pending_notify ntf;
    std::lock_guard lk(_mx);
    try {
        if (_error_state) std::rethrow_exception(_error_state);
        if (_state == not_established) {
            begin_ssl();
            if (_handshake.register_target_async(_writer_unlock_target) == nullptr) return;
        }
        while (true) {
            if (_state == closing || _state == closed) {
                ntf = _write_result(false);
                break;
            }
            int r = SSL_shutdown(_ssl);
            if (r > 0) {
                _state = closed;
                ntf = _write_result(true);
            } else if (r == 0) {
                _state = closing;
                ntf = _write_result(true);
            } else {
                coro::target_simple_activation(_writer_unlock_target, [&](coro::mutex::ownership){
                    write_eof_begin();
                });
                if (handle_ssl_error(r, _writer_unlock_target, [&]{
                    _state = closed;
                    ntf = _write_result(false);
                })) return;
            }
        }
    } catch (...) {
        _state = closed;
        ntf = _write_result.reject();
    }

}




_Stream Stream::accept(_Stream s, Context ctx) {
    auto x = std::make_shared<Stream>(s, ctx);
    x->accept_mode();
    return _Stream(x);
}

_Stream Stream::connect(_Stream s, Context ctx) {
    auto x = std::make_shared<Stream>(s, ctx);
    x->connect_mode();
    return _Stream(x);
}

_Stream Stream::connect(_Stream s, Context ctx, const std::string &hostname) {
    auto x = std::make_shared<Stream>(s, ctx);
    x->connect_mode(hostname);
    return _Stream(x);
}
_Stream Stream::accept(_Stream s, Context ctx, const Certificate &server_cert) {
    auto x = std::make_shared<Stream>(s, ctx);
    x->accept_mode(server_cert);
    return _Stream(x);
}

_Stream Stream::connect(_Stream s, Context ctx, const std::string &hostname, const Certificate &client_cert) {
    auto x = std::make_shared<Stream>(s, ctx);
    x->connect_mode(hostname, client_cert);
    return _Stream(x);
}

void Stream::accept_mode() {
    SSL_set_accept_state(_ssl);
}

void Stream::connect_mode() {
    SSL_set_connect_state(_ssl);
}

void Stream::connect_mode(const std::string &hostname) {
    SSL_set_connect_state(_ssl);
    SSL_set_tlsext_host_name(_ssl, hostname.c_str()); // @suppress("C-Style cast instead of C++ cast")
    X509_VERIFY_PARAM_set1_host(SSL_get0_param(_ssl), hostname.c_str(), 0);
    SSL_set_verify(_ssl, SSL_VERIFY_PEER, NULL);
}


void Stream::connect_mode(const std::string &hostname, const Certificate &client_cert) {
    connect_mode(hostname);

    if (client_cert.crt) SSL_use_certificate(_ssl, client_cert.crt);
    if (client_cert.pk) SSL_use_PrivateKey(_ssl, client_cert.pk);
}

void Stream::accept_mode(const Certificate &server_cert) {
    SSL_set_accept_state(_ssl);

    if (server_cert.crt) SSL_use_certificate(_ssl, server_cert.crt);
    if (server_cert.pk) SSL_use_PrivateKey(_ssl, server_cert.pk);
}

Stream::~Stream() {

}

coro::generator<_Stream> Stream::accept(coro::generator<_Stream> gen, Context ctx, std::function<void()> ssl_error) {
    auto f = gen();
    while (co_await f.has_value()) {
        try {
            co_yield Stream::accept(std::move(f.get()), ctx);
        } catch (...) {
            if (ssl_error) ssl_error();
        }
        f = gen();
    }
    co_return;

}


}



}



