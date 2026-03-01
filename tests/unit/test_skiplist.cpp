#include "store/Skiplist.h"

#include <cassert>
#include <cstdio>
#include <set>
#include <string>
#include <utility>
#include <vector>

static int testsPassed = 0;

#define TEST(name)                                            \
    do {                                                      \
        std::printf("  %-50s", name);                         \
    } while (0)

#define PASS()                                                \
    do {                                                      \
        std::printf("PASS\n");                                \
        ++testsPassed;                                        \
    } while (0)

// ── Basic insert and find ──────────────────────────────────────────────────
static void testInsertAndFind() {
    TEST("insert and find single element");
    Skiplist sl;
    sl.insert("alice", 10.0);
    assert(sl.size() == 1);
    assert(sl.find("alice", 10.0) != nullptr);
    assert(sl.find("alice", 20.0) == nullptr);
    assert(sl.find("bob", 10.0) == nullptr);
    PASS();
}

// ── Ordering ───────────────────────────────────────────────────────────────
static void testOrdering() {
    TEST("elements are ordered by (score, member)");
    Skiplist sl;
    sl.insert("charlie", 3.0);
    sl.insert("alice",   1.0);
    sl.insert("bob",     2.0);

    auto range = sl.rangeByRank(0, 2);
    assert(range.size() == 3);
    assert(range[0].first == "alice"   && range[0].second == 1.0);
    assert(range[1].first == "bob"     && range[1].second == 2.0);
    assert(range[2].first == "charlie" && range[2].second == 3.0);
    PASS();
}

// ── Same score, lexicographic tiebreak ─────────────────────────────────────
static void testDuplicateScores() {
    TEST("same score: ordered lexicographically");
    Skiplist sl;
    sl.insert("banana", 5.0);
    sl.insert("apple",  5.0);
    sl.insert("cherry", 5.0);

    auto range = sl.rangeByRank(0, 2);
    assert(range.size() == 3);
    assert(range[0].first == "apple");
    assert(range[1].first == "banana");
    assert(range[2].first == "cherry");
    PASS();
}

// ── Remove ─────────────────────────────────────────────────────────────────
static void testRemove() {
    TEST("remove existing element");
    Skiplist sl;
    sl.insert("a", 1.0);
    sl.insert("b", 2.0);
    sl.insert("c", 3.0);

    assert(sl.remove("b", 2.0) == true);
    assert(sl.size() == 2);
    assert(sl.find("b", 2.0) == nullptr);

    auto range = sl.rangeByRank(0, 1);
    assert(range.size() == 2);
    assert(range[0].first == "a");
    assert(range[1].first == "c");
    PASS();

    TEST("remove non-existing element returns false");
    assert(sl.remove("x", 99.0) == false);
    assert(sl.size() == 2);
    PASS();

    TEST("remove wrong score returns false");
    assert(sl.remove("a", 999.0) == false);
    assert(sl.size() == 2);
    PASS();
}

// ── RangeByRank with negative indices ──────────────────────────────────────
static void testNegativeIndices() {
    TEST("rangeByRank with negative indices");
    Skiplist sl;
    sl.insert("a", 1.0);
    sl.insert("b", 2.0);
    sl.insert("c", 3.0);
    sl.insert("d", 4.0);
    sl.insert("e", 5.0);

    // -2 to -1 = last two elements
    auto range = sl.rangeByRank(-2, -1);
    assert(range.size() == 2);
    assert(range[0].first == "d");
    assert(range[1].first == "e");
    PASS();

    TEST("rangeByRank 0 to -1 = all elements");
    range = sl.rangeByRank(0, -1);
    assert(range.size() == 5);
    PASS();

    TEST("rangeByRank out of bounds returns empty");
    range = sl.rangeByRank(10, 20);
    assert(range.empty());
    PASS();
}

// ── Move semantics ─────────────────────────────────────────────────────────
static void testMoveSemantics() {
    TEST("move constructor");
    Skiplist sl;
    sl.insert("a", 1.0);
    sl.insert("b", 2.0);

    Skiplist sl2(std::move(sl));
    assert(sl2.size() == 2);
    assert(sl2.find("a", 1.0) != nullptr);
    // sl is moved-from, size should be 0
    assert(sl.size() == 0);
    PASS();

    TEST("move assignment");
    Skiplist sl3;
    sl3.insert("x", 10.0);
    sl3 = std::move(sl2);
    assert(sl3.size() == 2);
    assert(sl3.find("b", 2.0) != nullptr);
    assert(sl2.size() == 0);
    PASS();
}

// ── Large insert stress test ───────────────────────────────────────────────
static void testLargeInsert() {
    TEST("insert 1000 elements and verify order");
    Skiplist sl;
    for (int i = 999; i >= 0; --i) {
        std::string member = "m" + std::to_string(i);
        sl.insert(member, static_cast<double>(i));
    }
    assert(sl.size() == 1000);

    auto range = sl.rangeByRank(0, 999);
    assert(range.size() == 1000);
    // Elements should be sorted by score ascending.
    for (int i = 0; i < 1000; ++i) {
        assert(range[i].second == static_cast<double>(i));
    }
    PASS();
}

// ── Empty skiplist ─────────────────────────────────────────────────────────
static void testEmptySkiplist() {
    TEST("operations on empty skiplist");
    Skiplist sl;
    assert(sl.size() == 0);
    assert(sl.find("x", 0.0) == nullptr);
    assert(sl.remove("x", 0.0) == false);

    auto range = sl.rangeByRank(0, -1);
    assert(range.empty());
    PASS();
}

int main() {
    std::printf("=== Skiplist Unit Tests ===\n");
    testInsertAndFind();
    testOrdering();
    testDuplicateScores();
    testRemove();
    testNegativeIndices();
    testMoveSemantics();
    testLargeInsert();
    testEmptySkiplist();
    std::printf("\n%d tests passed.\n", testsPassed);
    return 0;
}
