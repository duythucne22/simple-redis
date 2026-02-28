#include "net/Buffer.h"
#include "proto/RespParser.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

static int passed = 0;
static int failed = 0;

static void check(const char* name, bool ok) {
    if (ok) {
        std::printf("[PASS] %s\n", name);
        ++passed;
    } else {
        std::printf("[FAIL] %s\n", name);
        ++failed;
    }
}

/// Helper: fill a buffer with a C-string (excluding null terminator).
static void fillBuffer(Buffer& buf, const char* data) {
    size_t len = std::strlen(data);
    buf.append(data, len);
}

// ── Test: Parse a simple RESP array command (SET foo bar) ──────────────
// Verifies that a well-formed *3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n
// is parsed into {"SET", "foo", "bar"} and bytes are consumed.
static void test_parse_resp_array_basic() {
    Buffer buf;
    fillBuffer(buf, "*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n");
    size_t before = buf.readableBytes();
    assert(before == 31);

    RespParser parser;
    auto result = parser.parse(buf);
    assert(result.has_value());
    assert(result->size() == 3);
    assert((*result)[0] == "SET");
    assert((*result)[1] == "foo");
    assert((*result)[2] == "bar");
    // All bytes consumed.
    assert(buf.readableBytes() == 0);
    check("parse_resp_array_basic", true);
}

// ── Test: Parse PING (single-element array) ───────────────────────────
// Verifies that *1\r\n$4\r\nPING\r\n parses to {"PING"}.
static void test_parse_ping() {
    Buffer buf;
    fillBuffer(buf, "*1\r\n$4\r\nPING\r\n");

    RespParser parser;
    auto result = parser.parse(buf);
    assert(result.has_value());
    assert(result->size() == 1);
    assert((*result)[0] == "PING");
    assert(buf.readableBytes() == 0);
    check("parse_ping", true);
}

// ── Test: Incomplete frame returns nullopt, buffer untouched ──────────
// Verifies INV-1: parser never consumes partial frames.
static void test_parse_incomplete_frame() {
    Buffer buf;
    fillBuffer(buf, "*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n");
    size_t before = buf.readableBytes();

    RespParser parser;
    auto result = parser.parse(buf);
    assert(!result.has_value());
    // Buffer must be untouched.
    assert(buf.readableBytes() == before);
    check("parse_incomplete_frame", true);
}

// ── Test: Incomplete bulk string (missing trailing \r\n) ──────────────
// Verifies partial data within a bulk string doesn't consume.
static void test_parse_incomplete_bulk_string() {
    Buffer buf;
    // $3\r\nfoo — missing trailing \r\n
    fillBuffer(buf, "*1\r\n$3\r\nfoo");
    size_t before = buf.readableBytes();

    RespParser parser;
    auto result = parser.parse(buf);
    assert(!result.has_value());
    assert(buf.readableBytes() == before);
    check("parse_incomplete_bulk_string", true);
}

// ── Test: Pipelining (two commands in one buffer) ─────────────────────
// Verifies the parser processes one command at a time and leaves
// the remaining data for the next parse() call.
static void test_parse_pipelining() {
    Buffer buf;
    fillBuffer(buf, "*2\r\n$3\r\nGET\r\n$3\r\nfoo\r\n"
                    "*2\r\n$3\r\nGET\r\n$3\r\nbar\r\n");

    RespParser parser;

    // First command.
    auto r1 = parser.parse(buf);
    assert(r1.has_value());
    assert(r1->size() == 2);
    assert((*r1)[0] == "GET");
    assert((*r1)[1] == "foo");

    // Second command — still in the buffer.
    assert(buf.readableBytes() > 0);
    auto r2 = parser.parse(buf);
    assert(r2.has_value());
    assert(r2->size() == 2);
    assert((*r2)[0] == "GET");
    assert((*r2)[1] == "bar");

    // Buffer should be empty now.
    assert(buf.readableBytes() == 0);

    // Third parse — nothing left.
    auto r3 = parser.parse(buf);
    assert(!r3.has_value());
    check("parse_pipelining", true);
}

// ── Test: Inline command parsing ──────────────────────────────────────
// Verifies that "PING\r\n" (inline) is parsed to {"PING"}.
static void test_parse_inline_single() {
    Buffer buf;
    fillBuffer(buf, "PING\r\n");

    RespParser parser;
    auto result = parser.parse(buf);
    assert(result.has_value());
    assert(result->size() == 1);
    assert((*result)[0] == "PING");
    assert(buf.readableBytes() == 0);
    check("parse_inline_single", true);
}

// ── Test: Inline command with multiple args ───────────────────────────
// Verifies "SET foo bar\r\n" inline is parsed to {"SET","foo","bar"}.
static void test_parse_inline_multi_args() {
    Buffer buf;
    fillBuffer(buf, "SET foo bar\r\n");

    RespParser parser;
    auto result = parser.parse(buf);
    assert(result.has_value());
    assert(result->size() == 3);
    assert((*result)[0] == "SET");
    assert((*result)[1] == "foo");
    assert((*result)[2] == "bar");
    check("parse_inline_multi_args", true);
}

// ── Test: Incomplete inline (no \r\n yet) ─────────────────────────────
// Verifies that without \r\n, inline parse returns nullopt.
static void test_parse_inline_incomplete() {
    Buffer buf;
    fillBuffer(buf, "PING");
    size_t before = buf.readableBytes();

    RespParser parser;
    auto result = parser.parse(buf);
    assert(!result.has_value());
    assert(buf.readableBytes() == before);
    check("parse_inline_incomplete", true);
}

// ── Test: Empty buffer returns nullopt ─────────────────────────────────
// Verifies that parsing an empty buffer is safe.
static void test_parse_empty_buffer() {
    Buffer buf;
    RespParser parser;
    auto result = parser.parse(buf);
    assert(!result.has_value());
    check("parse_empty_buffer", true);
}

// ── Test: Binary-safe bulk string ─────────────────────────────────────
// Verifies that bulk strings containing \r\n within the data are parsed
// correctly (read exactly N bytes, don't scan for \r\n).
static void test_parse_binary_safe_bulk_string() {
    Buffer buf;
    // A 5-byte bulk string: "ab\r\nc"
    // Wire format: *1\r\n$5\r\nab\r\nc\r\n
    const char wire[] = "*1\r\n$5\r\nab\r\nc\r\n";
    buf.append(wire, sizeof(wire) - 1);  // exclude null terminator

    RespParser parser;
    auto result = parser.parse(buf);
    assert(result.has_value());
    assert(result->size() == 1);
    assert((*result)[0].size() == 5);
    assert((*result)[0] == std::string("ab\r\nc"));
    assert(buf.readableBytes() == 0);
    check("parse_binary_safe_bulk_string", true);
}

// ── Test: Empty bulk string ($0\r\n\r\n) ──────────────────────────────
// Verifies that a zero-length bulk string is valid and returns "".
static void test_parse_empty_bulk_string() {
    Buffer buf;
    fillBuffer(buf, "*1\r\n$0\r\n\r\n");

    RespParser parser;
    auto result = parser.parse(buf);
    assert(result.has_value());
    assert(result->size() == 1);
    assert((*result)[0].empty());
    assert(buf.readableBytes() == 0);
    check("parse_empty_bulk_string", true);
}

// ── Test: Null array (*-1\r\n) returns empty vector ───────────────────
// Verifies that a null array is handled gracefully.
static void test_parse_null_array() {
    Buffer buf;
    fillBuffer(buf, "*-1\r\n");

    RespParser parser;
    auto result = parser.parse(buf);
    assert(result.has_value());
    assert(result->empty());
    assert(buf.readableBytes() == 0);
    check("parse_null_array", true);
}

// ── Test: Large argument count ────────────────────────────────────────
// Verifies parsing works with many arguments (e.g., DEL key1 key2 ... key10).
static void test_parse_many_args() {
    Buffer buf;
    std::string wire = "*11\r\n$3\r\nDEL\r\n";
    for (int i = 0; i < 10; ++i) {
        std::string key = "key" + std::to_string(i);
        wire += "$" + std::to_string(key.size()) + "\r\n" + key + "\r\n";
    }
    buf.append(wire.data(), wire.size());

    RespParser parser;
    auto result = parser.parse(buf);
    assert(result.has_value());
    assert(result->size() == 11);
    assert((*result)[0] == "DEL");
    assert((*result)[1] == "key0");
    assert((*result)[10] == "key9");
    assert(buf.readableBytes() == 0);
    check("parse_many_args", true);
}

int main() {
    std::printf("=== RespParser Unit Tests ===\n");

    test_parse_resp_array_basic();
    test_parse_ping();
    test_parse_incomplete_frame();
    test_parse_incomplete_bulk_string();
    test_parse_pipelining();
    test_parse_inline_single();
    test_parse_inline_multi_args();
    test_parse_inline_incomplete();
    test_parse_empty_buffer();
    test_parse_binary_safe_bulk_string();
    test_parse_empty_bulk_string();
    test_parse_null_array();
    test_parse_many_args();

    std::printf("\n%d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
