#pragma once

namespace coroserver {

/// Represents the possible states of a stream (e.g., network connection, file stream, etc.).
enum class StreamState {
    /// The stream is in the process of being opened but is not yet fully available.
    opening,
    /// The stream is open and ready for reading and/or writing operations.
    active,
    /// The stream is in the process of closing; data may still be available for reading.
    closing,
    /// The stream is fully closed; no further input or output operations are possible.
    closed
};


}
