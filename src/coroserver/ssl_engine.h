#pragma once

#include <coroserver/stream_state.h>
#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <stdexcept>
#include <vector>

using coroserver::StreamState;


class SSLEngine {
public:
    enum TypeClient {client};
    enum TypeServer {server};



    SSLEngine(TypeClient, SSL_CTX *ctx, std::string host_name = {});

    SSLEngine(TypeServer, SSL_CTX *ctx);

    ~SSLEngine();

    ///current TLS state
    /**
     * @retval opening handshake is in progress. Call data_exchange() repeatedly
     * until other state is returned
     * @retval active session is full active
     * @retval closing session has been closed by close()
     * @retval closed session is fully closed
     */
    StreamState get_state();


    ///Request close SSL session
    /** Generates close sequence to terminate SSL session
     *
     * @note you need to call data_exchange to pass data to the network
     **/
    void close();


    ///Perform data exchange
    /** Call this function when data arrived from network.
     * @param data data arrived from network. For client, this is initially
     * empty. Function changes variable to view containing data to be send
     * @return get_state()
     */
    StreamState data_exchange(std::string_view &data);

    ///encrypt data
    /**
     * @param data data to encrypt
     * @note you need to call data_exchange after encrypt to receive
     * encrypted view.
     */
    StreamState encrypt(std::string_view data);


    ///decrypt data pushed by data_exchange.
    /**
     * @param data variable is set to contain view on decrypted data
     * @return stream state. If the view is set to empty, check the state.
     * If contains active state, probably more incoming data are required.
     * Request data from source and call data_exchange() when arrived
     */
    StreamState decrypt(std::string_view &data);



private:
    SSL *_ssl = nullptr;   // The SSL object
    BIO *_rbio = nullptr;  // Memory BIO for incoming encrypted data
    BIO *_wbio = nullptr;  // Memory BIO for outgoing encrypted data
    StreamState _state = {};
    bool _clear_output = false;
    std::vector<char> _decrypt_buffer;

    // Helper: update the state of the engine based on current conditions.
    void updateState();

    // Helper: perform (or continue) the handshake if needed.
    int doHandshake();

    void common_init(SSL_CTX *ctx);
    bool handle_error(int err);
    void clear_output();
};


