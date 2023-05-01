#include "ssl_stream.h"

 #include <openssl/bio.h>
#include <openssl/x509_vfy.h>
#include <cocls/coro_storage.h>

namespace coroserver {

namespace ssl {


Stream::Stream(_Stream target, Context ctx):AbstractProxyStream(target.getStreamDevice()) {
    _ssl = SSL_new(ctx);
    _read_data = BIO_new(BIO_s_mem());
    _write_data = BIO_new(BIO_s_mem());

    BIO_set_mem_eof_return(_read_data, -1);
    BIO_set_mem_eof_return(_write_data, -1);
    SSL_set_bio(_ssl, _read_data, _write_data);

}


cocls::future<std::string_view> Stream::read() {
    std::string_view tmp = AbstractStream::read_putback_buffer();
    if (!tmp.empty()) return cocls::future<std::string_view>::set_value(tmp);

    return run_ssl_io<std::string_view>(_rdstor, [this](std::string_view &ret) mutable {
        int r = SSL_read(_ssl, _rdbuff.data(), _rdbuff.size());
        if (r > 0) {
            ret = std::string_view(_rdbuff.data(), r);
            return _run_ssl_result_complete;
        }
        return 0;
    },tmp);
}
cocls::future<bool> Stream::write(std::string_view data) {
    return run_ssl_io<bool>(_wrstor, [this,data = std::string_view(data)](bool &ret) mutable {
        //write up 16384 bytes as the max TLS packet size is that size
        int r =  SSL_write(_ssl, data.data(), std::min<std::size_t>(data.size(),16384));
        if (r > 0) {
            data = data.substr(r);
            ret = true;
            return data.empty()?_run_ssl_result_complete:_run_ssl_result_retry;
        }
        return r;
    },false);
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


cocls::future<bool> Stream::write_eof() {
    if (_state == established) {
        return run_ssl_io<bool>(_wrstor, [this](bool &b){
            _state = closing;
            b = true;
            return SSL_shutdown(_ssl);
        },false);
    } else {
        return cocls::future<bool>::set_value(true);
    }
}

template<typename Ret, typename Fn>
cocls::with_allocator<cocls::reusable_storage, cocls::async<Ret> > Stream::run_ssl_io(cocls::reusable_storage &, Fn fn, Ret failRet) {
    std::unique_lock lk(_mx);
    Ret retval = failRet;
    bool rep = true;
    do {
        int r = _state==not_established?SSL_do_handshake(_ssl):fn(retval);

        char *buff;
        long sz = BIO_get_mem_data(_write_data,&buff); // @suppress("C-Style cast instead of C++ cast")
        if (sz) {
            lk.unlock();
            auto own = co_await _wrmx.lock();
            bool b = co_await _proxied->write(std::string_view(buff,sz));
            if (!b) {
                retval = failRet;
                break;
            }
            lk.lock();
            BIO_reset(_write_data);             // @suppress("C-Style cast instead of C++ cast")
        }

        if (r != _run_ssl_result_retry) {
            if (r > 0) {
                if (_state == not_established) {
                    _state = established;
                } else {
                    break;
                }
            } else {
                int status = SSL_get_error(_ssl, r);
                switch (status) {
                    case SSL_ERROR_ZERO_RETURN:
                        rep = false;
                        _state= closed;
                        break;
                    case SSL_ERROR_WANT_READ: {
                        lk.unlock();
                        auto own = co_await _rdmx.lock();
                        std::string_view data = co_await _proxied->read();
                        lk.lock();
                        if (data.empty()) {
                            _state = closed;
                            retval = failRet;
                            rep = false;
                        } else {
                            BIO_write(_read_data, data.data(), data.length());
                        }

                    }break;
                    case SSL_ERROR_SYSCALL:
                        _state = closed;
                        throw std::system_error(errno, std::system_category());
                    default:
                    case SSL_ERROR_SSL:
                        _state = closed;
                        throw SSLError();
                }
            }
        }
    }while (rep);

    if constexpr(std::is_void_v<Ret>) {
        co_return;
    } else {
        co_return retval;
    }

}


cocls::generator<_Stream> Stream::accept(cocls::generator<_Stream> gen, Context ctx, cocls::function<void()> ssl_error) {
    bool b = co_await gen.next();
    while (b) {
        try {
            co_yield Stream::accept(std::move(gen.value()), ctx);
        } catch (...) {
            if (ssl_error) ssl_error();
        }
        b = co_await gen.next();
    }
    co_return;

}


}



}



