/// Unit tests for Buffer — the only Phase 1 component with pure-logic
/// (non-syscall) behaviour worth testing in isolation.
///
/// Test framework: lightweight macros — no external dependencies.

#include "net/Buffer.h"

#include <cassert>
#include <cstdio>
#include <cstring>

// ── Minimal test harness ───────────────────────────────────────────────────
static int g_pass = 0;
static int g_fail = 0;

#define EXPECT(cond)                                                         \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("  FAILED: %s  (%s:%d)\n", #cond, __FILE__,         \
                        __LINE__);                                           \
            return false;                                                    \
        }                                                                    \
    } while (0)

#define RUN(fn)                                                              \
    do {                                                                     \
        if (fn()) {                                                          \
            g_pass++;                                                        \
            std::printf("[PASS] %s\n", #fn);                                 \
        } else {                                                             \
            g_fail++;                                                        \
            std::printf("[FAIL] %s\n", #fn);                                 \
        }                                                                    \
    } while (0)

// ── Tests ──────────────────────────────────────────────────────────────────

/// Fresh buffer has 0 readable and 0 writable bytes (no pre-allocation).
static bool test_fresh_buffer_is_empty() {
    Buffer buf;
    EXPECT(buf.readableBytes() == 0);
    EXPECT(buf.writableBytes() == 0);
    return true;
}

/// advanceWrite increases readable bytes, decreases writable bytes.
static bool test_advance_write() {
    Buffer buf;
    buf.ensureWritableBytes(64);
    size_t writable = buf.writableBytes();
    EXPECT(writable >= 64);

    buf.advanceWrite(10);
    EXPECT(buf.readableBytes() == 10);
    EXPECT(buf.writableBytes() == writable - 10);
    return true;
}

/// consume advances the read cursor and reduces readable bytes.
static bool test_consume() {
    Buffer buf;
    const char* msg = "hello";
    buf.append(msg, 5);
    EXPECT(buf.readableBytes() == 5);

    buf.consume(3);
    EXPECT(buf.readableBytes() == 2);
    EXPECT(std::memcmp(buf.readablePtr(), "lo", 2) == 0);
    return true;
}

/// Tier 1: consuming everything resets both cursors to 0.
static bool test_tier1_reset_on_empty() {
    Buffer buf;
    buf.append("abcdef", 6);
    buf.consume(6);

    EXPECT(buf.readableBytes() == 0);
    // After Tier 1 reset, the full capacity is writable again.
    EXPECT(buf.writableBytes() > 0);

    // Verify by appending new data — it should start at the front.
    buf.append("X", 1);
    EXPECT(buf.readableBytes() == 1);
    EXPECT(std::memcmp(buf.readablePtr(), "X", 1) == 0);
    return true;
}

/// Tier 2: compaction reclaims consumed space via memmove.
static bool test_tier2_compact() {
    Buffer buf;

    // Fill the buffer to its initial capacity.
    const size_t cap = 4096;
    char fill[4096];
    std::memset(fill, 'A', cap);
    buf.append(fill, cap);

    // Consume most of it, leaving only 100 bytes readable.
    buf.consume(cap - 100);
    EXPECT(buf.readableBytes() == 100);

    // Now ask for space that would exceed the back-end capacity but
    // fits within total capacity after compaction.
    buf.ensureWritableBytes(cap - 200);
    EXPECT(buf.writableBytes() >= cap - 200);
    // Readable data must be preserved.
    EXPECT(buf.readableBytes() == 100);
    return true;
}

/// Tier 3: when compaction isn't enough, the buffer grows.
static bool test_tier3_resize() {
    Buffer buf;

    // Fill to initial capacity.
    char fill[4096];
    std::memset(fill, 'B', sizeof(fill));
    buf.append(fill, sizeof(fill));
    EXPECT(buf.readableBytes() == 4096);

    // Request more writable space than the current total capacity.
    buf.ensureWritableBytes(4096);
    EXPECT(buf.writableBytes() >= 4096);
    // Existing data still intact.
    EXPECT(buf.readableBytes() == 4096);
    EXPECT(buf.readablePtr()[0] == 'B');
    return true;
}

/// append correctly copies data and updates cursors.
static bool test_append() {
    Buffer buf;
    buf.append("hello", 5);
    buf.append(" world", 6);

    EXPECT(buf.readableBytes() == 11);
    EXPECT(std::memcmp(buf.readablePtr(), "hello world", 11) == 0);
    return true;
}

/// Multiple produce/consume cycles exercise all tiers.
static bool test_multiple_cycles() {
    Buffer buf;
    for (int i = 0; i < 1000; ++i) {
        buf.append("ABCDEFGHIJ", 10);   // 10 bytes each
        buf.consume(10);
    }
    EXPECT(buf.readableBytes() == 0);

    // After many cycles of exact-size consume, buffer should be compact.
    buf.append("final", 5);
    EXPECT(buf.readableBytes() == 5);
    EXPECT(std::memcmp(buf.readablePtr(), "final", 5) == 0);
    return true;
}

// ── Main ───────────────────────────────────────────────────────────────────

int main() {
    std::printf("=== Buffer unit tests ===\n");

    RUN(test_fresh_buffer_is_empty);
    RUN(test_advance_write);
    RUN(test_consume);
    RUN(test_tier1_reset_on_empty);
    RUN(test_tier2_compact);
    RUN(test_tier3_resize);
    RUN(test_append);
    RUN(test_multiple_cycles);

    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
