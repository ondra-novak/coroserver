#include "ssl_stream.h"

 #include <openssl/bio.h>
#include <openssl/x509_vfy.h>


namespace coroserver {

namespace ssl {

bool Stream::eof_without_shudown_is_error = false;

Stream::Stream(_Stream target, Context ctx):AbstractProxyStream(target.getStreamDevice()) {
    _ssl = SSL_new(ctx);
    _read_data = BIO_new(BIO_s_mem());
    _write_data = BIO_new(BIO_s_mem());

    BIO_set_mem_eof_return(_read_data, -1);
    BIO_set_mem_eof_return(_write_data, -1);
    SSL_set_bio(_ssl, _read_data, _write_data);

}

template<typename RetVal>
coro::async<RetVal, coro::reusable_allocator> Stream::io_coroutine(coro::reusable_allocator &) {
    static constexpr bool reading = std::is_same_v<RetVal, std::string_view>;
    static constexpr bool writing = std::is_same_v<RetVal, bool>;
    static_assert(reading || writing, "Invalid usage");

    //this coroutine is called for reading or writing

    int r;

    //not established yet?
    if (_state == State::not_established) {
        //lock handshake - in case when read and write are running in parallel
        auto own = co_await _handshake;
        //repeat while not established
        while (_state == State::not_established) {
            //perform hanshake (no lock need there)
            r = SSL_do_handshake(_ssl);
            //check status, only 1 is established
            if (r > 0) {
                _state = State::established;
            } else {
                //by return value determine state
                Action action = determine_ssl_state(r);
                switch (action) {
                    case Action::write:
                        post_ssl_write(co_await send_encrypted());
                        break;

                    case Action::read:
                        post_ssl_read(co_await read_encrypted());
                        break;
                    default:
                        //invalid state - close connection
                        _state = State::closed;
                        break;
                }
            }
        }
    }

    do {
        if constexpr(reading) {
            {//SSL under lock
                std::lock_guard _(_mx);
                //return eof, if closed
                if (_state == State::closed) co_return std::string_view();
                //prepare buffer
                _read_buffer.resize(_read_buffer_size);
                //read from ssl
                r = SSL_read(_ssl,_read_buffer.data(), _read_buffer.size());
            }
            //success read?
            if (r > 0) {
                //return read data
                auto sz = static_cast<std::size_t>(r);
                if (sz == _read_buffer_size) {
                    _read_buffer_size = _read_buffer_size *3 /2;
                }
                co_return std::string_view(_read_buffer.data(), sz);
            }
        } else if constexpr(writing) {
            {//SSL under lock
                std::lock_guard _(_mx);
                //fail write is closing
                if (_state == State::closing || _state == State::closed) co_return false;
                //success write if emptyy
                if (_wrbuff.empty()) co_return true;
                //write buffer
                r = SSL_write(_ssl, _wrbuff.data(), _wrbuff.size());
            }
            //update _wrbuff depend on state
            if (r > 0) _wrbuff = _wrbuff.substr(r);
        }
        //determine ssl state now
        Action action = determine_ssl_state(r);
        switch (action) {
            case Action::write: {
                auto own = co_await _wrmx;
                post_ssl_write(co_await send_encrypted());
            } break;
            case Action::read: {
                auto own = co_await _rdmx;
                auto s = co_await read_encrypted();
                if (s.empty()) {
                    if constexpr (reading) {
                        //is empty returned, return also empty
                        //this might be timeout
                        co_return std::string_view();
                    } else {
                        //timeout when write requested is failure
                        co_return false;
                    }
                }
                //process data
                post_ssl_read(s);
                break;
            }
            default:
                //ignore any other state
                break;

        }
    }
     while (true);

}
//determine state
Stream::Action Stream::determine_ssl_state(int r) {
    //ownership - will be released outside lock
    coro::mutex::ownership own;
    std::lock_guard _(_mx);
    char *buff;
    long sz = BIO_get_mem_data(_write_data,&buff); // @suppress("C-Style cast instead of C++ cast")
    //any encrypted data to send?
    if (sz) {
        //try to lock write
        own = _wrmx.try_lock();
        //if failed, buffer is currently used, can't flush
        if (own) {
            //we acquired lock, resize buffer
            _encrypted_write_buffer.resize(sz);
            //copy content
            std::copy(buff, buff+sz, _encrypted_write_buffer.begin());
            //reset bio
            BIO_reset(_write_data);
            //request write
            return Action::write;
        }
    }
    // no action for this status
    if (r > 0) return Action::no_action;

    //determine error
    int ssl_state = SSL_get_error(_ssl, r);
    switch (ssl_state) {
        case SSL_ERROR_ZERO_RETURN: {
            _state = State::closed; //stream is closed
            return Action::eof;
        }
        case SSL_ERROR_WANT_READ: {
            return Action::read;    //we need read
        }
        case SSL_ERROR_SYSCALL:
            throw std::system_error(errno, std::system_category()); //system error
        default:
            throw SSLError();   //ssl error
    }
}

void Stream::post_ssl_read(std::string_view data) {
    std::lock_guard _(_mx);
    //read empty?
    if (data.empty()) {
        //steam is closed
        BIO_set_mem_eof_return(_read_data,0);
    } else {
        //otherwise set data
        BIO_write(_read_data, data.data(), data.length());
    }
}

coro::future<bool> Stream::send_encrypted() {
    return _proxied->write(std::string_view(_encrypted_write_buffer.data(), _encrypted_write_buffer.size()));
}
coro::future<std::string_view> Stream::read_encrypted() {
    return _proxied->read();
}


coro::async<bool, coro::reusable_allocator> Stream::write_eof_coro(coro::reusable_allocator &) {
    int r;
    {
        std::lock_guard _(_mx);
        //we need established state
        if (_state != State::established) co_return true;
        //shutdown SSL stream
        r = SSL_shutdown(_ssl);
        //state is closing
        _state = State::closing;
    }
    //we need flush buffer
    Action a = determine_ssl_state(r);
    //and if something need to be written, write it now
    if (a == Action::write) {
        auto own = co_await _wrmx;
        co_return post_ssl_write(co_await send_encrypted());
    }
    co_return true;
}

coro::future<bool> Stream::write_eof() {
    return write_eof_coro(_wrstor);
}

bool Stream::post_ssl_write(bool st) {
    std::lock_guard _(_mx);
    _encrypted_write_buffer.clear();
    if (st == false && _state != State::closed) {
        _state = State::closing;
    }
    return st;
}


coro::future<std::string_view> Stream::read() {
    std::string_view tmp = AbstractStream::read_putback_buffer();
    if (!tmp.empty() || _state == State::closed) return tmp;
    return io_coroutine<std::string_view>(_rdstor);
}

coro::future<bool> Stream::write(std::string_view data) {
    _wrbuff = data;
    return io_coroutine<bool>(_wrstor);
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
    //if the connection is still established
    //we need to shutdown this
    //but we have no longer asynchronous features available (in destructor)
    if (_state == State::established) {
        //shutdown of the stream causes that all operations becomes non-blocking
        _proxied->shutdown();
        //try to shutdown SSL connection - shutdown packet is sent nonblocking
        //this should not wait, as socket has asynchronous operations disabled
        write_eof().wait();
    }
}

coro::generator<_Stream> Stream::accept(coro::generator<_Stream> gen, Context ctx, std::function<void()> ssl_error) {
    auto f = gen();
    while (co_await !!f) {
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



