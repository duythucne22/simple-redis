#include "proto/RespParser.h"

#include <cstdlib>
#include <cstring>

// ── Helper: find \r\n within [data+offset, data+len) ──────────────────────
int RespParser::findCRLF(const uint8_t* data, size_t len, size_t offset) {
    // Need at least 2 bytes for \r\n.
    if (len < offset + 2) return -1;
    for (size_t i = offset; i + 1 < len; ++i) {
        if (data[i] == '\r' && data[i + 1] == '\n') {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// ── Parse RESP array ──────────────────────────────────────────────────────
std::optional<std::vector<std::string>>
RespParser::parseArray(const uint8_t* data, size_t len, size_t& bytesConsumed) {
    // data[0] == '*'. Find the first \r\n to read the element count.
    int crlfPos = findCRLF(data, len, 1);
    if (crlfPos < 0) return std::nullopt;  // incomplete

    // Parse the element count: *N\r\n
    // N is between data[1] and data[crlfPos-1].
    std::string countStr(reinterpret_cast<const char*>(data + 1),
                         static_cast<size_t>(crlfPos - 1));
    int count = std::atoi(countStr.c_str());
    if (count < 0) {
        // *-1\r\n is a null array — treat as empty command.
        bytesConsumed = static_cast<size_t>(crlfPos) + 2;
        return std::vector<std::string>{};
    }

    // Now parse `count` bulk strings.
    size_t pos = static_cast<size_t>(crlfPos) + 2;  // past *N\r\n
    std::vector<std::string> args;
    args.reserve(static_cast<size_t>(count));

    for (int i = 0; i < count; ++i) {
        if (pos >= len) return std::nullopt;  // incomplete

        if (data[pos] != '$') {
            // Not a bulk string — protocol error. Try to recover.
            return std::nullopt;
        }

        // Find \r\n after $len
        int lenCRLF = findCRLF(data, len, pos + 1);
        if (lenCRLF < 0) return std::nullopt;  // incomplete

        // Parse the bulk string length.
        std::string lenStr(reinterpret_cast<const char*>(data + pos + 1),
                           static_cast<size_t>(lenCRLF) - (pos + 1));
        int bulkLen = std::atoi(lenStr.c_str());

        if (bulkLen < 0) {
            // $-1\r\n = null bulk string.
            args.emplace_back("");
            pos = static_cast<size_t>(lenCRLF) + 2;
            continue;
        }

        // The data starts after $len\r\n and is exactly bulkLen bytes,
        // followed by \r\n.
        size_t dataStart = static_cast<size_t>(lenCRLF) + 2;
        size_t dataEnd   = dataStart + static_cast<size_t>(bulkLen);

        // Need dataEnd + 2 bytes (for trailing \r\n).
        if (dataEnd + 2 > len) return std::nullopt;  // incomplete

        // Verify trailing \r\n (binary safety: we do NOT scan for \r\n
        // within the bulk data — we read exactly bulkLen bytes).
        if (data[dataEnd] != '\r' || data[dataEnd + 1] != '\n') {
            // Protocol error — malformed bulk string.
            return std::nullopt;
        }

        args.emplace_back(reinterpret_cast<const char*>(data + dataStart),
                          static_cast<size_t>(bulkLen));
        pos = dataEnd + 2;
    }

    bytesConsumed = pos;
    return args;
}

// ── Parse inline command ──────────────────────────────────────────────────
std::optional<std::vector<std::string>>
RespParser::parseInline(const uint8_t* data, size_t len, size_t& bytesConsumed) {
    // Read until \r\n, then split on spaces.
    int crlfPos = findCRLF(data, len, 0);
    if (crlfPos < 0) return std::nullopt;  // incomplete

    std::string line(reinterpret_cast<const char*>(data),
                     static_cast<size_t>(crlfPos));
    bytesConsumed = static_cast<size_t>(crlfPos) + 2;

    // Split on spaces.
    std::vector<std::string> args;
    size_t pos = 0;
    while (pos < line.size()) {
        // Skip leading spaces.
        while (pos < line.size() && line[pos] == ' ') ++pos;
        if (pos >= line.size()) break;
        // Find end of token.
        size_t end = line.find(' ', pos);
        if (end == std::string::npos) end = line.size();
        args.push_back(line.substr(pos, end - pos));
        pos = end;
    }

    return args;
}

// ── Main parse entry point ────────────────────────────────────────────────
std::optional<std::vector<std::string>> RespParser::parse(Buffer& buf) {
    size_t readable = buf.readableBytes();
    if (readable == 0) return std::nullopt;

    const uint8_t* data = buf.readablePtr();
    size_t bytesConsumed = 0;

    std::optional<std::vector<std::string>> result;

    if (data[0] == '*') {
        result = parseArray(data, readable, bytesConsumed);
    } else {
        result = parseInline(data, readable, bytesConsumed);
    }

    if (result.has_value()) {
        // Only consume bytes after a successful, complete parse.
        buf.consume(bytesConsumed);
    }

    return result;
}
