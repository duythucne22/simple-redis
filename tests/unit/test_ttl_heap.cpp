// tests/unit/test_ttl_heap.cpp
// Unit tests for TTLHeap — deterministic timestamps, no sockets.

#include "store/TTLHeap.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>

static int passed = 0;
static int failed = 0;

#define RUN_TEST(name)                                \
    do {                                              \
        std::printf("  %-55s", #name);                \
        try {                                         \
            name();                                   \
            std::printf("[PASS]\n");                   \
            ++passed;                                 \
        } catch (...) {                               \
            std::printf("[FAIL]\n");                   \
            ++failed;                                 \
        }                                             \
    } while (0)

// ── Test: empty heap returns empty on popExpired ──
// Verifies that popExpired on an empty heap returns nothing and does not crash.
static void test_empty_heap() {
    TTLHeap heap;
    assert(heap.empty());
    assert(heap.size() == 0);
    auto expired = heap.popExpired(1000);
    assert(expired.empty());
}

// ── Test: push and popExpired with single entry ──
// Verifies basic push/pop cycle with one key.
static void test_push_and_pop_single() {
    TTLHeap heap;
    heap.push("key1", 100);
    assert(heap.size() == 1);
    assert(!heap.empty());

    // Not yet expired at time 99.
    auto expired = heap.popExpired(99);
    assert(expired.empty());
    assert(heap.size() == 1);

    // Expired at time 100 (<=).
    expired = heap.popExpired(100);
    assert(expired.size() == 1);
    assert(expired[0] == "key1");
    assert(heap.empty());
}

// ── Test: min-heap property — earliest deadline pops first ──
// Verifies INV-1: heap orders by expireAtMs ascending.
static void test_min_heap_ordering() {
    TTLHeap heap;
    heap.push("late", 300);
    heap.push("early", 100);
    heap.push("middle", 200);

    auto expired = heap.popExpired(350);
    assert(expired.size() == 3);
    assert(expired[0] == "early");
    assert(expired[1] == "middle");
    assert(expired[2] == "late");
}

// ── Test: remove by key ──
// Verifies that remove(key) works and the heap stays consistent.
static void test_remove() {
    TTLHeap heap;
    heap.push("a", 100);
    heap.push("b", 200);
    heap.push("c", 300);

    heap.remove("b");
    assert(heap.size() == 2);

    auto expired = heap.popExpired(350);
    assert(expired.size() == 2);
    // "b" should not appear.
    for (const auto& key : expired) {
        assert(key != "b");
    }
}

// ── Test: remove non-existent key is a no-op ──
// Verifies no crash or corruption when removing a key not in the heap.
static void test_remove_nonexistent() {
    TTLHeap heap;
    heap.push("a", 100);
    heap.remove("does_not_exist");
    assert(heap.size() == 1);
    auto expired = heap.popExpired(200);
    assert(expired.size() == 1);
    assert(expired[0] == "a");
}

// ── Test: push duplicate key delegates to update ──
// Verifies INV-4: no duplicate keys in the heap.
static void test_push_duplicate_updates() {
    TTLHeap heap;
    heap.push("key1", 100);
    heap.push("key1", 50);  // should update, not add a second entry
    assert(heap.size() == 1);

    // Should expire at the updated (earlier) time of 50.
    auto expired = heap.popExpired(55);
    assert(expired.size() == 1);
    assert(expired[0] == "key1");
    assert(heap.empty());
}

// ── Test: update changes deadline ──
// Verifies update correctly repositions the entry in the heap.
static void test_update() {
    TTLHeap heap;
    heap.push("a", 100);
    heap.push("b", 200);

    // Move "b" ahead of "a".
    heap.update("b", 50);

    auto expired = heap.popExpired(75);
    assert(expired.size() == 1);
    assert(expired[0] == "b");

    expired = heap.popExpired(150);
    assert(expired.size() == 1);
    assert(expired[0] == "a");
}

// ── Test: update non-existent key acts as push ──
// Verifies update for a new key inserts it.
static void test_update_nonexistent() {
    TTLHeap heap;
    heap.update("newkey", 42);
    assert(heap.size() == 1);
    auto expired = heap.popExpired(42);
    assert(expired.size() == 1);
    assert(expired[0] == "newkey");
}

// ── Test: popExpired respects maxWork bound ──
// Verifies INV-8: at most maxWork entries are returned.
static void test_popExpired_maxWork() {
    TTLHeap heap;
    for (int i = 0; i < 100; ++i) {
        heap.push("key" + std::to_string(i), 10 + i);
    }

    // All 100 keys are expired at time 200, but maxWork = 5.
    auto expired = heap.popExpired(200, 5);
    assert(expired.size() == 5);
    assert(heap.size() == 95);

    // Pop more.
    expired = heap.popExpired(200, 10);
    assert(expired.size() == 10);
    assert(heap.size() == 85);
}

// ── Test: popExpired stops at non-expired entries ──
// Verifies that entries with future deadlines are not popped.
static void test_popExpired_stops_at_future() {
    TTLHeap heap;
    heap.push("expired1", 100);
    heap.push("expired2", 200);
    heap.push("future", 500);

    auto expired = heap.popExpired(300);
    assert(expired.size() == 2);
    assert(heap.size() == 1);

    // The remaining entry is "future".
    expired = heap.popExpired(300);
    assert(expired.empty());
}

// ── Test: remove last remaining entry ──
// Verifies removing the only entry works.
static void test_remove_last_entry() {
    TTLHeap heap;
    heap.push("only", 100);
    heap.remove("only");
    assert(heap.empty());
    assert(heap.size() == 0);
}

// ── Test: many pushes with remove, checking consistency ──
// Verifies INV-3: heap_.size() == keyToIndex_.size() after many operations.
static void test_stress_consistency() {
    TTLHeap heap;
    // Push 1000 keys.
    for (int i = 0; i < 1000; ++i) {
        heap.push("key" + std::to_string(i), 1000 + i);
    }
    assert(heap.size() == 1000);

    // Remove every other key.
    for (int i = 0; i < 1000; i += 2) {
        heap.remove("key" + std::to_string(i));
    }
    assert(heap.size() == 500);

    // Pop all expired (all at time 2000+).
    auto expired = heap.popExpired(3000, 1000);
    assert(expired.size() == 500);
    assert(heap.empty());
}

// ── Test: heap ordering after interleaved push and remove ──
// Verifies that the min-heap property holds after complex operations.
static void test_ordering_after_remove() {
    TTLHeap heap;
    heap.push("a", 50);
    heap.push("b", 30);
    heap.push("c", 40);
    heap.push("d", 10);
    heap.push("e", 20);

    // Remove the minimum.
    heap.remove("d");

    auto expired = heap.popExpired(55);
    // Remaining order should be: e(20), b(30), c(40), a(50)
    assert(expired.size() == 4);
    assert(expired[0] == "e");
    assert(expired[1] == "b");
    assert(expired[2] == "c");
    assert(expired[3] == "a");
}

// ── Test: update to later deadline ──
// Verifies updating to a later time sifts down correctly.
static void test_update_to_later() {
    TTLHeap heap;
    heap.push("a", 100);
    heap.push("b", 200);
    heap.push("c", 300);

    // Move "a" to after "c".
    heap.update("a", 400);

    auto expired = heap.popExpired(250);
    assert(expired.size() == 1);
    assert(expired[0] == "b");

    expired = heap.popExpired(350);
    assert(expired.size() == 1);
    assert(expired[0] == "c");

    expired = heap.popExpired(450);
    assert(expired.size() == 1);
    assert(expired[0] == "a");
}

int main() {
    std::printf("=== TTLHeap Unit Tests ===\n");

    RUN_TEST(test_empty_heap);
    RUN_TEST(test_push_and_pop_single);
    RUN_TEST(test_min_heap_ordering);
    RUN_TEST(test_remove);
    RUN_TEST(test_remove_nonexistent);
    RUN_TEST(test_push_duplicate_updates);
    RUN_TEST(test_update);
    RUN_TEST(test_update_nonexistent);
    RUN_TEST(test_popExpired_maxWork);
    RUN_TEST(test_popExpired_stops_at_future);
    RUN_TEST(test_remove_last_entry);
    RUN_TEST(test_stress_consistency);
    RUN_TEST(test_ordering_after_remove);
    RUN_TEST(test_update_to_later);

    std::printf("\nResults: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
