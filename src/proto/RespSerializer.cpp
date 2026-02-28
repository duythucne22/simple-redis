#include "proto/RespSerializer.h"

#include <string>

void RespSerializer::writeSimpleString(Buffer& buf, std::string_view s) {
    buf.append("+", 1);
    buf.append(s.data(), s.size());
    buf.append("\r\n", 2);
}

void RespSerializer::writeError(Buffer& buf, std::string_view msg) {
    buf.append("-", 1);
    buf.append(msg.data(), msg.size());
    buf.append("\r\n", 2);
}

void RespSerializer::writeInteger(Buffer& buf, int64_t val) {
    std::string s = ":" + std::to_string(val) + "\r\n";
    buf.append(s.data(), s.size());
}

void RespSerializer::writeBulkString(Buffer& buf, std::string_view s) {
    std::string header = "$" + std::to_string(s.size()) + "\r\n";
    buf.append(header.data(), header.size());
    buf.append(s.data(), s.size());
    buf.append("\r\n", 2);
}

void RespSerializer::writeNull(Buffer& buf) {
    buf.append("$-1\r\n", 5);
}

void RespSerializer::writeArrayHeader(Buffer& buf, int64_t count) {
    std::string s = "*" + std::to_string(count) + "\r\n";
    buf.append(s.data(), s.size());
}
