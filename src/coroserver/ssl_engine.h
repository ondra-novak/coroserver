#pragma once

#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <stdexcept>

/*
 * SSLEngine is a synchronous engine that wraps an SSL/TLS session.
 * It uses memory BIOs so that you can feed it encrypted data (e.g., from a socket,
 * pipe, or any IPC mechanism) and have it produce decrypted plaintext, and vice‐versa.
 */
class SSLEngine {
public:
    // The engine’s state reflects whether it is waiting for more data
    // or is ready to process application data.
    enum EngineState {
        NEED_ENCRYPTED_DATA,   // Need encrypted data to proceed (e.g. handshake or decryption)
        NEED_PLAIN_DATA,       // There is plaintext waiting to be encrypted (encrypted data pending)
        HANDSHAKE_IN_PROGRESS, // The SSL handshake is not yet complete
        READY,                 // Fully handshaked and ready for application data
        ERROR_STATE            // An error has occurred
    };

    /**
     * Constructor.
     *
     * @param ctx       A valid SSL_CTX pointer.
     * @param isServer  If true, the engine acts as a server (using SSL_accept);
     *                  otherwise, it acts as a client (using SSL_connect).
     * @throws std::runtime_error if the SSL object or the BIOs cannot be created.
     */
    SSLEngine(SSL_CTX *ctx, bool isServer);

    ~SSLEngine();

    /**
     * Feed encrypted data received from the remote peer into the engine.
     *
     * @param data   Pointer to the encrypted data.
     * @param length Length in bytes.
     * @return       The number of bytes written into the internal buffer, or a negative value on error.
     */
    int submitEncrypted(const void *data, size_t length);

    /**
     * Attempt to read decrypted (plaintext) data.
     *
     * @param buf     Buffer to store the decrypted data.
     * @param bufLen  Size of the buffer.
     * @return        The number of bytes read, zero if no data is available,
     *                or a negative value on error.
     */
    int readDecrypted(void *buf, size_t bufLen);

    /**
     * Feed plaintext (decrypted) data to be encrypted.
     *
     * @param data   Pointer to the plaintext data.
     * @param length Length in bytes.
     * @return       The number of bytes accepted, or a negative value on error.
     */
    int submitPlain(const void *data, size_t length);

    /**
     * Read encrypted data produced by the engine.
     *
     * @param buf     Buffer to store the encrypted output.
     * @param bufLen  Size of the buffer.
     * @return        The number of bytes read, zero if no data is available,
     *                or a negative value on error.
     */
    int readEncrypted(void *buf, size_t bufLen);

    /**
     * Query the current engine state.
     *
     * @return The current EngineState.
     */
    EngineState getState() const;

private:
    SSL *ssl_;   // The SSL object
    BIO *rbio_;  // Memory BIO for incoming encrypted data
    BIO *wbio_;  // Memory BIO for outgoing encrypted data
    bool isServer_;
    EngineState state_;

    // Helper: update the state of the engine based on current conditions.
    void updateState();

    // Helper: perform (or continue) the handshake if needed.
    int doHandshake();
};


