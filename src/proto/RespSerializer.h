#pragma once

#include "net/Buffer.h"

#include <cstdint>
#include <string_view>

/// Serializes RESP2 responses into a Buffer.
/// All methods are static — no state needed.
///
/// Must NOT know about: Commands, the database, networking.
class RespSerializer {
public:
    /// Write a simple string response: +msg\r\n
    static void writeSimpleString(Buffer& buf, std::string_view s);

    /// Write an error response: -msg\r\n
    static void writeError(Buffer& buf, std::string_view msg);

    /// Write an integer response: :val\r\n
    static void writeInteger(Buffer& buf, int64_t val);

    /// Write a bulk string response: $len\r\ndata\r\n
    static void writeBulkString(Buffer& buf, std::string_view s);

    /// Write a null bulk string: $-1\r\n
    static void writeNull(Buffer& buf);

    /// Write an array header: *count\r\n
    /// Caller writes individual elements after this.
    static void writeArrayHeader(Buffer& buf, int64_t count);
};
