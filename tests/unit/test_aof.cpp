// tests/unit/test_aof.cpp
//
// Unit tests for AOF RESP encoding round-trip.
// Verifies that AOFWriter::log() produces correct RESP that RespParser
// can parse back to the original arguments.
//
// No sockets, no processes — pure logic tests.

#include "persistence/AOFWriter.h"
#include "proto/RespParser.h"
#include "net/Buffer.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <unistd.h>
#include <vector>

static int g_passed = 0;
static int g_failed = 0;

static void pass(const char* name) {
    std::printf("[PASS] %s\n", name);
    ++g_passed;
}

static void fail(const char* name, const char* reason) {
    std::printf("[FAIL] %s — %s\n", name, reason);
    ++g_failed;
}

/// Helper: write args to a temp file via AOFWriter, read it back, parse
/// with RespParser, and compare against the original args.
/// Returns true if the round-trip succeeded.
static bool roundTrip(const std::vector<std::string>& args,
                      std::vector<std::string>& parsed) {
    // Create a temp file.
    char tmpPath[] = "/tmp/test_aof_XXXXXX";
    int tmpFd = ::mkstemp(tmpPath);
    if (tmpFd < 0) return false;
    ::close(tmpFd);

    // Write via AOFWriter.
    {
        AOFWriter writer(tmpPath, AOFWriter::FsyncPolicy::ALWAYS);
        assert(writer.isEnabled());
        writer.log(args);
    }  // destructor closes + fsyncs

    // Read the file into a Buffer.
    int fd = ::open(tmpPath, O_RDONLY);
    if (fd < 0) { ::unlink(tmpPath); return false; }

    Buffer buf;
    uint8_t readBuf[4096];
    while (true) {
        ssize_t n = ::read(fd, readBuf, sizeof(readBuf));
        if (n <= 0) break;
        buf.append(readBuf, static_cast<size_t>(n));
    }
    ::close(fd);
    ::unlink(tmpPath);

    // Parse with RespParser.
    RespParser parser;
    auto result = parser.parse(buf);
    if (!result.has_value()) return false;

    parsed = *result;
    return true;
}

// ── Test: basic SET command round-trip ───────────────────────────────────
// Verifies that a simple 3-argument command encodes and decodes correctly.
static void test_basic_set_roundtrip() {
    const char* name = "basic_set_roundtrip";
    std::vector<std::string> args = {"SET", "foo", "bar"};
    std::vector<std::string> parsed;

    if (!roundTrip(args, parsed)) { fail(name, "round-trip failed"); return; }
    if (parsed.size() != 3) { fail(name, "wrong arg count"); return; }
    if (parsed[0] != "SET" || parsed[1] != "foo" || parsed[2] != "bar") {
        fail(name, "arg mismatch"); return;
    }
    pass(name);
}

// ── Test: empty string argument ─────────────────────────────────────────
// Verifies that $0\r\n\r\n (zero-length bulk string) is handled correctly.
static void test_empty_string_argument() {
    const char* name = "empty_string_argument";
    std::vector<std::string> args = {"SET", "key", ""};
    std::vector<std::string> parsed;

    if (!roundTrip(args, parsed)) { fail(name, "round-trip failed"); return; }
    if (parsed.size() != 3) { fail(name, "wrong arg count"); return; }
    if (parsed[2] != "") { fail(name, "empty string mismatch"); return; }
    pass(name);
}

// ── Test: binary data in value ──────────────────────────────────────────
// Verifies that values with spaces, newlines, and \r\n are preserved.
static void test_binary_safe_value() {
    const char* name = "binary_safe_value";
    std::string value = "hello world\r\nwith newlines\ttabs";
    std::vector<std::string> args = {"SET", "mykey", value};
    std::vector<std::string> parsed;

    if (!roundTrip(args, parsed)) { fail(name, "round-trip failed"); return; }
    if (parsed.size() != 3) { fail(name, "wrong arg count"); return; }
    if (parsed[2] != value) { fail(name, "binary data corrupted"); return; }
    pass(name);
}

// ── Test: single-argument command (PING) ────────────────────────────────
// Verifies that a 1-argument command encodes correctly.
static void test_single_arg_command() {
    const char* name = "single_arg_command";
    std::vector<std::string> args = {"PING"};
    std::vector<std::string> parsed;

    if (!roundTrip(args, parsed)) { fail(name, "round-trip failed"); return; }
    if (parsed.size() != 1) { fail(name, "wrong arg count"); return; }
    if (parsed[0] != "PING") { fail(name, "arg mismatch"); return; }
    pass(name);
}

// ── Test: multi-argument command (DEL with multiple keys) ───────────────
// Verifies that variable-arity commands encode correctly.
static void test_multi_arg_command() {
    const char* name = "multi_arg_command";
    std::vector<std::string> args = {"DEL", "k1", "k2", "k3", "k4"};
    std::vector<std::string> parsed;

    if (!roundTrip(args, parsed)) { fail(name, "round-trip failed"); return; }
    if (parsed.size() != 5) { fail(name, "wrong arg count"); return; }
    for (size_t i = 0; i < args.size(); ++i) {
        if (parsed[i] != args[i]) { fail(name, "arg mismatch"); return; }
    }
    pass(name);
}

// ── Test: multiple commands in one file ─────────────────────────────────
// Verifies that multiple log() calls produce a file with multiple commands
// that can all be parsed sequentially.
static void test_multiple_commands_in_file() {
    const char* name = "multiple_commands_in_file";

    char tmpPath[] = "/tmp/test_aof_multi_XXXXXX";
    int tmpFd = ::mkstemp(tmpPath);
    if (tmpFd < 0) { fail(name, "mkstemp failed"); return; }
    ::close(tmpFd);

    // Write three commands.
    {
        AOFWriter writer(tmpPath, AOFWriter::FsyncPolicy::ALWAYS);
        writer.log({"SET", "a", "1"});
        writer.log({"SET", "b", "2"});
        writer.log({"DEL", "a"});
    }

    // Read file into buffer.
    int fd = ::open(tmpPath, O_RDONLY);
    if (fd < 0) { fail(name, "open failed"); ::unlink(tmpPath); return; }

    Buffer buf;
    uint8_t readBuf[4096];
    while (true) {
        ssize_t n = ::read(fd, readBuf, sizeof(readBuf));
        if (n <= 0) break;
        buf.append(readBuf, static_cast<size_t>(n));
    }
    ::close(fd);
    ::unlink(tmpPath);

    // Parse all three commands.
    RespParser parser;

    auto cmd1 = parser.parse(buf);
    if (!cmd1 || cmd1->size() != 3 || (*cmd1)[0] != "SET") {
        fail(name, "cmd1 parse failed"); return;
    }

    auto cmd2 = parser.parse(buf);
    if (!cmd2 || cmd2->size() != 3 || (*cmd2)[0] != "SET") {
        fail(name, "cmd2 parse failed"); return;
    }

    auto cmd3 = parser.parse(buf);
    if (!cmd3 || cmd3->size() != 2 || (*cmd3)[0] != "DEL") {
        fail(name, "cmd3 parse failed"); return;
    }

    // No more commands should be parseable.
    auto cmd4 = parser.parse(buf);
    if (cmd4.has_value()) { fail(name, "unexpected 4th command"); return; }

    pass(name);
}

// ── Test: EXPIRE command round-trip ─────────────────────────────────────
// Verifies that TTL commands are correctly encoded.
static void test_expire_roundtrip() {
    const char* name = "expire_command_roundtrip";
    std::vector<std::string> args = {"PEXPIRE", "mykey", "3600000"};
    std::vector<std::string> parsed;

    if (!roundTrip(args, parsed)) { fail(name, "round-trip failed"); return; }
    if (parsed.size() != 3) { fail(name, "wrong arg count"); return; }
    if (parsed[0] != "PEXPIRE" || parsed[1] != "mykey" || parsed[2] != "3600000") {
        fail(name, "arg mismatch"); return;
    }
    pass(name);
}

// ── Test: large value ───────────────────────────────────────────────────
// Verifies that values larger than typical buffers encode correctly.
static void test_large_value() {
    const char* name = "large_value_roundtrip";
    std::string bigVal(10000, 'X');  // 10KB value
    std::vector<std::string> args = {"SET", "bigkey", bigVal};
    std::vector<std::string> parsed;

    if (!roundTrip(args, parsed)) { fail(name, "round-trip failed"); return; }
    if (parsed.size() != 3) { fail(name, "wrong arg count"); return; }
    if (parsed[2] != bigVal) { fail(name, "large value corrupted"); return; }
    pass(name);
}

// ── Test: RESP format verification ──────────────────────────────────────
// Verifies the exact byte format of the RESP encoding.
static void test_exact_resp_format() {
    const char* name = "exact_resp_format";

    char tmpPath[] = "/tmp/test_aof_fmt_XXXXXX";
    int tmpFd = ::mkstemp(tmpPath);
    if (tmpFd < 0) { fail(name, "mkstemp failed"); return; }
    ::close(tmpFd);

    {
        AOFWriter writer(tmpPath, AOFWriter::FsyncPolicy::ALWAYS);
        writer.log({"SET", "k", "v"});
    }

    // Read raw bytes.
    int fd = ::open(tmpPath, O_RDONLY);
    if (fd < 0) { fail(name, "open failed"); ::unlink(tmpPath); return; }

    char raw[256];
    ssize_t n = ::read(fd, raw, sizeof(raw));
    ::close(fd);
    ::unlink(tmpPath);

    // Expected: *3\r\n$3\r\nSET\r\n$1\r\nk\r\n$1\r\nv\r\n
    std::string expected = "*3\r\n$3\r\nSET\r\n$1\r\nk\r\n$1\r\nv\r\n";
    std::string actual(raw, static_cast<size_t>(n));

    if (actual != expected) {
        fail(name, "format mismatch");
        std::fprintf(stderr, "  expected (%zu bytes): ", expected.size());
        for (char c : expected) {
            if (c == '\r') std::fprintf(stderr, "\\r");
            else if (c == '\n') std::fprintf(stderr, "\\n");
            else std::fprintf(stderr, "%c", c);
        }
        std::fprintf(stderr, "\n  actual   (%zu bytes): ", actual.size());
        for (char c : actual) {
            if (c == '\r') std::fprintf(stderr, "\\r");
            else if (c == '\n') std::fprintf(stderr, "\\n");
            else std::fprintf(stderr, "%c", c);
        }
        std::fprintf(stderr, "\n");
        return;
    }
    pass(name);
}

int main() {
    std::printf("=== AOF Unit Tests ===\n");

    test_basic_set_roundtrip();
    test_empty_string_argument();
    test_binary_safe_value();
    test_single_arg_command();
    test_multi_arg_command();
    test_multiple_commands_in_file();
    test_expire_roundtrip();
    test_large_value();
    test_exact_resp_format();

    std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return g_failed > 0 ? 1 : 0;
}
