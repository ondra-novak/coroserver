/*
 * ssl_common.h
 *
 *  Created on: 20. 11. 2022
 *      Author: ondra
 */

#ifndef SRC_USERVER_SSL_COMMON_H_
#define SRC_USERVER_SSL_COMMON_H_
#include "stream.h"

#include <stdexcept>
#include <vector>

typedef struct ssl_ctx_st SSL_CTX;
typedef struct ssl_st SSL;
typedef struct x509_st X509;
typedef struct evp_pkey_st EVP_PKEY;
typedef struct bio_st BIO;

namespace coroserver {

namespace ssl {

    namespace _details {

        struct def_SSL_CTX {
            using ptr_type = SSL_CTX *;
            static void add_ref(ptr_type);
            static void release_ref(ptr_type);
        };

        struct def_X509 {
            using ptr_type = X509 *;
            static void add_ref(ptr_type);
            static void release_ref(ptr_type);
        };

        struct def_SSL {
            using ptr_type = SSL *;
            static void add_ref(ptr_type);
            static void release_ref(ptr_type);
        };

        struct def_EVP_PKEY {
            using ptr_type = EVP_PKEY *;
            static void add_ref(ptr_type);
            static void release_ref(ptr_type);
        };
        struct def_BIO {
            using ptr_type = BIO *;
            static void add_ref(ptr_type);
            static void release_ref(ptr_type);
        };

        template<typename Def>
        class OpenSSLObject {
        public:
            using ptr_type = typename Def::ptr_type;

            OpenSSLObject() = default;
            OpenSSLObject(ptr_type v):_v(v) {}
            OpenSSLObject(const OpenSSLObject &other):_v(other._v) {
                if (_v) Def::add_ref(_v);
            }
            OpenSSLObject(OpenSSLObject &&other):_v(other._v) {
                other._v = nullptr;
            }
            OpenSSLObject &operator=(const OpenSSLObject &other){
                if (this != &other) {
                    if (_v) Def::release_ref(_v);
                    _v = other._v;
                    if (_v) Def::add_ref(_v);
                }
                return *this;
            }
            OpenSSLObject &operator=(OpenSSLObject &&other){
                if (this != &other) {
                    if (_v) Def::release_ref(_v);
                    _v = other._v;
                    other._v = nullptr;
                }
                return *this;
            }
            ~OpenSSLObject() {
                if (_v) Def::release_ref(_v);
            }

            operator ptr_type() const {
                return _v;
            }

            operator bool() const {
                return _v != nullptr;
            }

            bool operator !() const {
                return _v == nullptr;
            }
            ptr_type release() {
                ptr_type out = _v;
                _v = nullptr;
                return out;
            }
        protected:
            ptr_type _v = nullptr;

        };
    }


    using x509_t = _details::OpenSSLObject<_details::def_X509>;
    using pkey_t = _details::OpenSSLObject<_details::def_EVP_PKEY>;

    struct Certificate {
        x509_t crt;
        pkey_t pk;
        std::vector<x509_t> chain;

        Certificate() = default;
        Certificate(std::string_view crt_PEM, std::string_view pk_PEM) {
            load_cert_from_string(crt_PEM);
            load_priv_from_string(pk_PEM);
        }

        void load_cert(BIO *source);
        void load_cert(const std::string &cert_file_PEM);
        void load_cert_from_string(const std::string_view &cert_PEM);
        void load_priv_key(BIO *source, const std::string &passphrase = std::string());
        void load_priv_key(const std::string &pk_file_PEM, const std::string &passphrase = std::string());
        void load_priv_from_string(const std::string_view &pk_PEM, const std::string &passphrase = std::string());

    };

    ///SSL context, provides conversion from stream to SSL stream
    /**
     * To construct the SSL context, use init_server() or init_client() function depend on
     * intention of usage
     */
    class Context: public _details::OpenSSLObject<_details::def_SSL_CTX> {
    public:
        using _details::OpenSSLObject<_details::def_SSL_CTX>::OpenSSLObject;

        ///Load verify certificates from a directory
        int load_verify_dir(const char *CApath);
        ///Load verify certificates from a file
        int load_verify_file(const char *CAfile);
        ///Load verify certificates from store file
        int load_verify_store(const char *CAstore);

        ///set context certificate
        /**
         * for the server, it specifies server certificate, for client, it specified client certificate
         * @param cert certificate bundle
         */
        void set_certificate(const Certificate &cert);

        ///initialize ssl libraries
        /** it is called automatically when init_server() or init_client() is called*/
        static void initSSL();


        ///Inicialize context for server
        static Context init_server();
        ///Inicialize context for client
        static Context init_client();
    };

    class SSLError: public std::runtime_error{
    public:
        SSLError():std::runtime_error(createSSLError()) {}

        static std::string createSSLError();
    };

    using BasicIO = _details::OpenSSLObject<_details::def_BIO>;
    using SSLObject = _details::OpenSSLObject<_details::def_SSL>;



}

}


#endif /* SRC_USERVER_SSL_COMMON_H_ */
