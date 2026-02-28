#pragma once

#include "net/Buffer.h"

#include <optional>
#include <string>
#include <vector>

/// Parses RESP2 commands from a Buffer.
///
/// Supports:
///   - RESP arrays of bulk strings (*N\r\n$len\r\n...\r\n)
///   - Inline commands (text\r\n, split on spaces)
///
/// If the buffer does not contain a complete frame, returns nullopt and
/// leaves the buffer untouched. Only consumes bytes on successful parse.
///
/// Must NOT know about: Sockets, epoll, commands, the database.
class RespParser {
public:
    /// Attempt to parse one complete command from the buffer.
    /// Returns nullopt if data is incomplete.
    /// On success, consumes the parsed bytes from the buffer.
    std::optional<std::vector<std::string>> parse(Buffer& buf);

private:
    /// Try to find \r\n starting at `offset` within readable bytes.
    /// Returns the offset of \r, or -1 if not found.
    static int findCRLF(const uint8_t* data, size_t len, size_t offset);

    /// Parse a RESP array (*N\r\n followed by N bulk strings).
    /// Returns nullopt if incomplete. Does NOT consume from buffer.
    /// Sets `bytesConsumed` to the total bytes of the complete frame.
    std::optional<std::vector<std::string>>
    parseArray(const uint8_t* data, size_t len, size_t& bytesConsumed);

    /// Parse an inline command (read until \r\n, split on spaces).
    /// Returns nullopt if incomplete. Does NOT consume from buffer.
    /// Sets `bytesConsumed` to the total bytes of the complete frame.
    std::optional<std::vector<std::string>>
    parseInline(const uint8_t* data, size_t len, size_t& bytesConsumed);
};
