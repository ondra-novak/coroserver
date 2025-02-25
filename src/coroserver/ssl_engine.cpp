#include "ssl_engine.h"
#include <openssl/err.h>
#include <cstring>

SSLEngine::SSLEngine(SSL_CTX *ctx, bool isServer)
    : ssl_(nullptr), rbio_(nullptr), wbio_(nullptr), isServer_(isServer),
      state_(HANDSHAKE_IN_PROGRESS)
{
    ssl_ = SSL_new(ctx);
    if (!ssl_) {
        throw std::runtime_error("Failed to create SSL object");
    }

    // Create memory BIOs for I/O.
    rbio_ = BIO_new(BIO_s_mem());
    wbio_ = BIO_new(BIO_s_mem());
    if (!rbio_ || !wbio_) {
        SSL_free(ssl_);
        throw std::runtime_error("Failed to create BIOs");
    }

    // Set our memory BIOs into the SSL object.
    // Note: SSL_set_bio() takes ownership of the BIOs.
    SSL_set_bio(ssl_, rbio_, wbio_);

    // Optional: set auto-retry mode.
    SSL_set_mode(ssl_, SSL_MODE_AUTO_RETRY);

    // Initiate the handshake.
    if (isServer_) {
        SSL_accept(ssl_);
    } else {
        SSL_connect(ssl_);
    }
    updateState();
}

SSLEngine::~SSLEngine() {
    if (ssl_) {
        SSL_free(ssl_); // This will free the associated BIOs as well.
    }
}

int SSLEngine::submitEncrypted(const void *data, size_t length) {
    // Write the incoming encrypted data into the read BIO.
    int written = BIO_write(rbio_, data, static_cast<int>(length));
    if (written <= 0) {
        return written;
    }

    // If the handshake isn’t complete, try to continue it.
    if (state_ == HANDSHAKE_IN_PROGRESS) {
        doHandshake();
    } else {
        // Optionally: if handshake is done, try peeking to see if any plaintext is now available.
        char dummy;
        SSL_peek(ssl_, &dummy, 1);
    }

    updateState();
    return written;
}

int SSLEngine::readDecrypted(void *buf, size_t bufLen) {
    // If the handshake is not yet complete, try to continue it.
    if (state_ == HANDSHAKE_IN_PROGRESS) {
        if (doHandshake() <= 0) {
            updateState();
            return 0; // No plaintext available yet.
        }
    }

    // Attempt to read decrypted data.
    int ret = SSL_read(ssl_, buf, static_cast<int>(bufLen));
    if (ret <= 0) {
        int err = SSL_get_error(ssl_, ret);
        if (err == SSL_ERROR_WANT_READ) {
            // More encrypted data is needed.
            updateState();
            return 0;
        } else if (err == SSL_ERROR_ZERO_RETURN) {
            // Connection closed cleanly.
            return 0;
        } else {
            // An error occurred.
            state_ = ERROR_STATE;
            return -1;
        }
    }
    updateState();
    return ret;
}

int SSLEngine::submitPlain(const void *data, size_t length) {
    // Only allow plaintext submission once the handshake is complete.
    if (state_ != READY) {
        return -1;
    }

    int ret = SSL_write(ssl_, data, static_cast<int>(length));
    if (ret <= 0) {
        int err = SSL_get_error(ssl_, ret);
        if (err == SSL_ERROR_WANT_WRITE) {
            // Likely the write BIO is full—encrypted data is waiting to be read.
            updateState();
            return 0;
        } else {
            state_ = ERROR_STATE;
            return -1;
        }
    }
    updateState();
    return ret;
}

int SSLEngine::readEncrypted(void *buf, size_t bufLen) {
    // Read any encrypted output from the write BIO.
    int ret = BIO_read(wbio_, buf, static_cast<int>(bufLen));
    if (ret <= 0) {
        // No encrypted data is available at the moment.
        return 0;
    }
    updateState();
    return ret;
}

SSLEngine::EngineState SSLEngine::getState() const {
    return state_;
}

void SSLEngine::updateState() {
    // If an error has already been recorded, do not change the state.
    if (state_ == ERROR_STATE)
        return;

    // If the handshake is not yet finished, report that.
    if (!SSL_is_init_finished(ssl_)) {
        state_ = HANDSHAKE_IN_PROGRESS;
    } else {
        // If there is pending encrypted data in the write BIO, then the engine has
        // encrypted data waiting to be sent.
        if (BIO_pending(wbio_) > 0) {
            state_ = NEED_PLAIN_DATA;
        }
        // If there is pending data in the read BIO, then we likely need more encrypted data
        // to complete an SSL_read.
        else if (BIO_pending(rbio_) > 0) {
            state_ = NEED_ENCRYPTED_DATA;
        } else {
            state_ = READY;
        }
    }
}

int SSLEngine::doHandshake() {
    int ret;
    if (isServer_) {
        ret = SSL_accept(ssl_);
    } else {
        ret = SSL_connect(ssl_);
    }
    if (ret <= 0) {
        int err = SSL_get_error(ssl_, ret);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            // Handshake is still in progress.
            ret = 0;
        } else {
            state_ = ERROR_STATE;
            ret = -1;
        }
    }
    return ret;
}
