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
    SSL_set_cert_cb(_ssl.get(), [](SSL *ssl, void *arg){
        SSLStream *me = reinterpret_cast<SSLStream *>(arg);
        const char *sni = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
        try {
            return me->_cert_cb(ssl, sni)?1:0;
        } catch (const std::exception &e) {
            ERR_put_error(ERR_LIB_SSL, 0, SSL_R_CERT_CB_ERROR, __FILE__, __LINE__);
            ERR_add_error_data(1, e.what());
            return 0;
        } catch (...) {
            ERR_put_error(ERR_LIB_SSL, 0, SSL_R_CERT_CB_ERROR, __FILE__, __LINE__);
            ERR_add_error_data(1, "Unknown exception in certificate callback");
            return 0;
        }
    }, this);
    SSL_accept(_ssl.get());
    run_handshake();
}

SSLStream::SSLStream(TypeClient, Stream s,
        SSL_CTX *ctx, std::string sni_host)
:StreamProxy(std::move(s))
{
    common_init(ctx);
    if (!sni_host.empty()) {
          if (SSL_set_tlsext_host_name(_ssl.get(), sni_host.c_str()) != 1) {
                  throw SSLError("Failed to set SNI");
          }
    }
    SSL_connect(_ssl.get());
    run_handshake();

}

SSLStream::TwoCoros SSLStream::fail_io(std::exception_ptr e) {
    _state = StreamState::closed;
    return {_recv_awaiting.set_exception(e),_send_awaiting.set_exception(e)};
}
SSLStream::TwoCoros SSLStream::fail_io() {
    _state = StreamState::closed;
    return {_recv_awaiting(), _send_awaiting(false)};
}
SSLStream::TwoCoros SSLStream::finish_handshake() {
    _handshake_running = false;
    long code = SSL_get_verify_result(_ssl.get());
    if (code == X509_V_OK) {
        TwoCoros out;
        _state = StreamState::active;
        //read any possible data after handshake
        auto s = read_ssl_nb();
        //this can be empty, but if there is awaiting caller
        if (s.empty() && _recv_awaiting) {
            //continue in async reading
            out.second =  _async_receive_cb.await(_s.receive(),this);
        } else {
            //otherwise send result to caller (if none, this command does nothing)
            out.second = _recv_awaiting(s);
        }
        //any data to send after handshake?
        if (!_send_awaiting_data.empty()) {
            //write them now
            handle_error(SSL_write(_ssl.get(), _send_awaiting_data.data(), _send_awaiting_data.size()));
            //and they are written
            _send_awaiting_data = {};
        }
        if (_req_close) {
            handle_error(SSL_shutdown(_ssl.get()));
            _state = StreamState::closing;
        }
        //any output data in buffer
        s = get_output_data();
        if (!s.empty()) {
            //send them to the stream
            out.first =  _async_send_cb.await(_s.send(s),this);
        } else {
            //this is success for send
            out.first =  _send_awaiting(true);
        }
        return out;
    } else {
        return fail_io(std::make_exception_ptr(
                SSLVerificationException(code,
                        X509_verify_cert_error_string(code))));
    }
}


SSLStream::TwoCoros SSLStream::async_process_receive(awaitable<std::string_view> &awt)
{
    std::lock_guard _(_mx);
    try {
        std::string_view s = awt.await_resume();
        if (s.empty()) {
            return fail_io();
        } else {
            //feed bio with data
            BIO_write(_rbio, s.data(), s.size());
            //for handshake phase
            if (_state == StreamState::opening) {
                //do handshake
                bool b = handle_error(SSL_do_handshake(_ssl.get()));
                //handshake success?
                if (b) {
                    return finish_handshake();
                } else if (_state == StreamState::opening) {
                    //handshake is not complete yet
                    //if there are output data
                    s = get_output_data();
                    if (!s.empty()) {
                        //send them async
                        return {_async_send_cb.await(_s.send(s), this)};
                    } else {
                        //otherwise wait for more incoming data
                        return {_async_receive_cb.await(_s.receive(),this)};
                    }
                } else {
                    return fail_io();
                }
            } else {
                //normal operation
                //read data nb
                s = read_ssl_nb();
                //empty data and still normal operation?
                if (s.empty() && _state == StreamState::active) {
                    //receive more data
                    auto p = _async_receive_cb.await(_s.receive(), this);
                    //if there is no send awaiting
                    if (!_send_awaiting) {
                        //try to pick output data
                        s = get_output_data();
                        //and if there are some, this can be reneg.
                        if (!s.empty()) {
                            //change state
                            _state = StreamState::opening;
                            //send data
                            return {_async_send_cb.await(_s.send(s), this),std::move(p)};
                        }
                    }
                    //if _send_awaiting is active, this issue will be solved on send side
                    return {std::move(p)};
                }
                //non-empty buffer is returned to caller
                return {_recv_awaiting(s)};
            }
        }
    } catch (...) {
        auto e = std::current_exception();
        return fail_io(e);
    }
}

SSLStream::TwoCoros SSLStream::async_process_send(awaitable<bool> &awt)
{
    std::lock_guard _(_mx);
    try {
        //read send result
        bool r = awt.await_resume();
        //send successful
        if (r) {
            //for handshake phase
            if (_state == StreamState::opening) {
                //do handshake
                bool b = handle_error(SSL_do_handshake(_ssl.get()));
                //if handshake finished
                if (b) {
                    return finish_handshake();
                //handshake is not complete but still in progress
                } else if (_state == StreamState::opening) {
                    //continue by reading
                    return {_async_receive_cb.await(_s.receive(), this)};
                } else {
                    return fail_io();
                }
            } else {
                //normal operation
                //test whether there are still data to send
                auto s = get_output_data();
                //if so...
                if (!s.empty()) {
                    //continue in sending
                    return {_async_send_cb.await(_s.send(s), this)};
                } else {
                    //no more data, report success
                    return {_send_awaiting(true)};
                }
            }
        } else {
            //unsuccessful writing means connection is broken
            return fail_io();
        }
    } catch (...) {
        //any exception means connection is broken
        auto e = std::current_exception();
        return fail_io(e);
    }
}

void SSLStream::common_init(SSL_CTX *ctx)
{

    _ssl.reset(SSL_new(ctx));
    if (!_ssl) {
        throw SSLError("Failed to create SSL object");
    }

    std::unique_ptr<BIO, BIODeleter> rb ( BIO_new(BIO_s_mem()) );
    std::unique_ptr<BIO, BIODeleter> wb ( BIO_new(BIO_s_mem()) );
    std::unique_ptr<BIO, BIODeleter> wb2 ( BIO_new(BIO_s_mem()) );

    if (!rb || !wb || !wb2) {
        throw SSLError("Failed to create BIOs");
    }

    _rbio = rb.release();
    _wbio = wb.release();
    _wbio2 = std::move(wb2);

    // Set our memory BIOs into the SSL object.
    // Note: SSL_set_bio() takes ownership of the BIOs.
    SSL_set_bio(_ssl.get(), _rbio, _wbio);

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
                prepared_coro c;
                if (!_handshake_running) {
                    c = run_handshake_except(res);
                }
                this->_recv_awaiting = std::move(res);
                return c;
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
                prepared_coro c;
                if (!_handshake_running) {
                    c = run_handshake_except(res);
                }
                this->_send_awaiting = std::move(res);
            };
        }
        if (data.empty()) return _s.send(data); //just sync

        bool st = handle_error(SSL_write(_ssl.get(), data.data(), data.size()));
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
    int e = SSL_get_error(_ssl.get(), retval);
    switch (e) {
        case SSL_ERROR_NONE: return true;
        case SSL_ERROR_ZERO_RETURN:
            _state = StreamState::closed;
            return true;
        case SSL_ERROR_WANT_READ:
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
    int l = SSL_read(_ssl.get(), _decrypt_buffer.data(), _decrypt_buffer.size());
    if (handle_error(l)) {
        return {_decrypt_buffer.data(),static_cast<std::size_t>(l)};
    } else {
        return {};
    }
}

prepared_coro SSLStream::run_handshake()
{
    //forced handshake - set apropriate state
    _state = StreamState::opening;
    //control handshake and process errors
    bool b = handle_error(SSL_do_handshake(_ssl.get()));

    if (!_send_awaiting) {
        auto s = get_output_data();
        if (!s.empty()) {
            return _async_send_cb.await(_s.send(s),this);
        }
    }
    if (!_recv_awaiting) {
        if (!b) {
            return _async_receive_cb.await(_s.receive(),this);
        }
    }
    return {};
}

template<typename Res>
prepared_coro SSLStream::run_handshake_except(Res &res) {
    try {
        return run_handshake();
    } catch (...) {
        return res.set_exception(std::current_exception());
    }
}

void SSLStream::put_back(std::string_view s) {
    _putback_buffer = s;
}

awaitable<bool> SSLStream::close() {
    while (true) {
        std::unique_lock lk(_mx);
        if (_state == StreamState::closed || _state == StreamState::closing) {
            return false;
        }
        if (_state == StreamState::opening) {
            this->_req_close = true;
            return [this, lk = std::move(lk)](SendResult res) {
                prepared_coro c;
                if (!_handshake_running) {
                    c = run_handshake_except(res);
                }
                this->_send_awaiting = std::move(res);
            };
        }
        bool st = handle_error(SSL_shutdown(_ssl.get()));
        if (st) {
            std::string_view  out_data = get_output_data();
            if (!out_data.empty()) return _s.send(out_data);
            _state = StreamState::closing;
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

std::string_view SSLStream::get_output_data() {
    BUF_MEM *buf1;
    BUF_MEM *buf2;
    BIO_reset(_wbio2.get());
    BIO_get_mem_ptr(_wbio,&buf1);
    BIO_get_mem_ptr(_wbio2.get(),&buf2);
    BIO_set_mem_buf(_wbio,buf2, BIO_CLOSE);
    BIO_set_mem_buf(_wbio2.get(),buf1, BIO_CLOSE);
    return {reinterpret_cast<const char *>(buf1->data),static_cast<std::size_t>(buf1->length)};
}



void SSLStream::SSLDeleter::operator ()(SSL *ssl) const {
    SSL_free(ssl);
}


void SSLStream::BIODeleter::operator ()(BIO *bio) const {
    BIO_free(bio);
}

}

