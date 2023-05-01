#include "ssl_common.h"

#include <memory>
#include <mutex>
#include <string>
#include <openssl/err.h>
#include <openssl/ssl.h>


namespace coroserver {
namespace ssl {

int Context::load_verify_dir(const char *CApath) {
    return SSL_CTX_load_verify_dir(*this, CApath);
}
int Context::load_verify_file(const char *CAfile) {
    return SSL_CTX_load_verify_file(*this, CAfile);
}
int Context::load_verify_store(const char *CAstore) {
    return SSL_CTX_load_verify_store(*this, CAstore);
}


Context Context::init_server() {
    initSSL();
    Context c(SSL_CTX_new(TLS_server_method()));
    SSL_CTX_set_default_verify_paths(c);
    return c;
}


Context Context::init_client() {
    initSSL();
    Context c(SSL_CTX_new(TLS_client_method()));
    SSL_CTX_set_default_verify_paths(c);
    return c;
}



std::string SSLError::createSSLError() {
    std::string errors;
    ERR_print_errors_cb([](const char *str, size_t len, void *u){
        std::string *s = reinterpret_cast<std::string *>(u);
        s->append(str,len);
        s->append("\n");
        return 1;
    }, &errors);
    return errors;
}


static std::once_flag _ssl_init;

void Context::initSSL() {
    std::call_once(_ssl_init, []{
        #if OPENSSL_VERSION_NUMBER < 0x10100000L
        SSL_library_init();
        #else
        OPENSSL_init_ssl(0, NULL);
        #endif
        SSL_load_error_strings();
    });
}


void Certificate::load_cert(const std::string &cert_file_PEM) {
    BasicIO b(BIO_new_file(cert_file_PEM.c_str(), "r"));
    int e = errno;
    if (!b) throw std::system_error(e, std::system_category(), "load_cert: " + cert_file_PEM);
    load_cert(b);
}

void Certificate::load_cert_from_string(const std::string_view &cert_PEM) {
    BasicIO b(BIO_new_mem_buf(cert_PEM.data(), cert_PEM.length()));
    load_cert(b);
}

void Certificate::load_cert(BIO *b) {
    crt = PEM_read_bio_X509(b, NULL, 0, NULL);
    if (!crt) throw SSLError();
    chain.clear();
    x509_t tmp = PEM_read_bio_X509(b, NULL, 0 , NULL);
    while (tmp) {
        chain.push_back(std::move(tmp));
        tmp = PEM_read_bio_X509(b, NULL, 0 , NULL);
    }
    auto err = ERR_peek_last_error();
    if (ERR_GET_LIB(err) == ERR_LIB_PEM
        && ERR_GET_REASON(err) == PEM_R_NO_START_LINE) {
        ERR_clear_error();
    } else {
        throw SSLError();
    }
}

void Certificate::load_priv_key(const std::string &pk_file_PEM, const std::string &passphrase) {
    BasicIO b (BIO_new_file(pk_file_PEM.c_str(), "r"));
    int e = errno;
    if (!b) throw std::system_error(e, std::system_category(), "load_priv_key: " + pk_file_PEM);
    load_priv_key(b,passphrase);
}

void Certificate::load_priv_from_string(const std::string_view &pk_PEM, const std::string &passphrase) {
    BasicIO b(BIO_new_mem_buf(pk_PEM.data(), pk_PEM.length()));
    load_priv_key(b, passphrase);

}

void Certificate::load_priv_key(BIO *b, const std::string &passphrase) {
    pk = PEM_read_bio_PrivateKey(b, NULL, 0, const_cast<void *>(reinterpret_cast<const void *>(passphrase.c_str())));
    if (!pk) throw SSLError();
}

void _details::def_SSL_CTX::add_ref(ptr_type x) {SSL_CTX_up_ref(x);}
void _details::def_SSL_CTX::release_ref(ptr_type x) { SSL_CTX_free(x);}
void _details::def_X509::add_ref(ptr_type x) {X509_up_ref(x);}
void _details::def_X509::release_ref(ptr_type x) { X509_free(x);}
void _details::def_SSL::add_ref(ptr_type x) { SSL_up_ref(x);}
void _details::def_SSL::release_ref(ptr_type x) {SSL_free(x);}
void _details::def_EVP_PKEY::add_ref(ptr_type x) { EVP_PKEY_up_ref(x);}
void _details::def_EVP_PKEY::release_ref(ptr_type x) {EVP_PKEY_free(x);}
void _details::def_BIO::add_ref(ptr_type x) {BIO_up_ref(x);}
void _details::def_BIO::release_ref(ptr_type x) {BIO_free(x);}


void Context::set_certificate(const Certificate &cert) {
    if (cert.crt) SSL_CTX_use_certificate(*this,cert.crt);
    if (cert.pk) SSL_CTX_use_PrivateKey(*this, cert.pk);
    SSL_CTX_clear_extra_chain_certs(*this);
    for (x509_t x: cert.chain) {
        SSL_CTX_add_extra_chain_cert(*this,x.release());
    }
}


}
}

