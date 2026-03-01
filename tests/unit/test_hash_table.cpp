#include "store/HashTable.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
#include <unordered_set>

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

// ── Test: Insert and find a single key ────────────────────────────────
// Verifies basic set/find functionality.
static void test_insert_and_find() {
    HashTable ht;
    ht.set("hello", RedisObject::createString("world"));

    HTEntry* entry = ht.find("hello");
    assert(entry != nullptr);
    assert(entry->key == "hello");
    assert(entry->value.asString() == "world");
    assert(ht.size() == 1);
    check("insert_and_find", true);
}

// ── Test: Find non-existent key returns nullptr ───────────────────────
// Verifies that find on a missing key behaves correctly.
static void test_find_nonexistent() {
    HashTable ht;
    ht.set("hello", RedisObject::createString("world"));
    HTEntry* entry = ht.find("nonexistent");
    assert(entry == nullptr);
    check("find_nonexistent", true);
}

// ── Test: Overwrite existing key ──────────────────────────────────────
// Verifies that setting the same key twice updates the value.
static void test_overwrite() {
    HashTable ht;
    ht.set("key", RedisObject::createString("val1"));
    ht.set("key", RedisObject::createString("val2"));

    HTEntry* entry = ht.find("key");
    assert(entry != nullptr);
    assert(entry->value.asString() == "val2");
    assert(ht.size() == 1);  // size didn't increase
    check("overwrite", true);
}

// ── Test: Delete existing key ─────────────────────────────────────────
// Verifies that del() removes the key and decrements size.
static void test_delete_existing() {
    HashTable ht;
    ht.set("key", RedisObject::createString("val"));
    assert(ht.size() == 1);

    bool deleted = ht.del("key");
    assert(deleted);
    assert(ht.size() == 0);
    assert(ht.find("key") == nullptr);
    check("delete_existing", true);
}

// ── Test: Delete non-existent key ─────────────────────────────────────
// Verifies that del() returns false for a missing key.
static void test_delete_nonexistent() {
    HashTable ht;
    ht.set("key", RedisObject::createString("val"));

    bool deleted = ht.del("missing");
    assert(!deleted);
    assert(ht.size() == 1);
    check("delete_nonexistent", true);
}

// ── Test: Keys collection ─────────────────────────────────────────────
// Verifies that keys() returns all stored keys.
static void test_keys_collection() {
    HashTable ht;
    ht.set("a", RedisObject::createString("1"));
    ht.set("b", RedisObject::createString("2"));
    ht.set("c", RedisObject::createString("3"));

    auto allKeys = ht.keys();
    assert(allKeys.size() == 3);

    std::unordered_set<std::string> keySet(allKeys.begin(), allKeys.end());
    assert(keySet.count("a") == 1);
    assert(keySet.count("b") == 1);
    assert(keySet.count("c") == 1);
    check("keys_collection", true);
}

// ── Test: Rehashing triggers and completes ────────────────────────────
// Verifies that inserting enough keys to exceed load factor 2.0
// triggers rehashing, and after enough rehash steps, all keys are
// still accessible.
static void test_rehash_triggers() {
    HashTable ht;
    // Initial capacity is 4. Load factor threshold is 2.0.
    // So inserting 9 keys (9 / 4 = 2.25 > 2.0) should trigger rehash.
    for (int i = 0; i < 20; ++i) {
        std::string key = "key" + std::to_string(i);
        ht.set(key, RedisObject::createString(std::to_string(i)));
    }

    // After many inserts, size must be correct.
    assert(ht.size() == 20);

    // Complete any pending rehashing.
    for (int i = 0; i < 1000; ++i) {
        ht.rehashStep(128);
    }

    // All keys must still be accessible.
    for (int i = 0; i < 20; ++i) {
        std::string key = "key" + std::to_string(i);
        HTEntry* entry = ht.find(key);
        assert(entry != nullptr);
        assert(entry->value.asString() == std::to_string(i));
    }
    assert(ht.size() == 20);
    check("rehash_triggers", true);
}

// ── Test: Large-scale insert / find / delete ──────────────────────────
// Verifies correctness at scale: 10,000 keys inserted, all found,
// half deleted, remaining still found.
static void test_large_scale() {
    HashTable ht;
    const int N = 10000;

    // Insert N keys.
    for (int i = 0; i < N; ++i) {
        std::string key = "k" + std::to_string(i);
        ht.set(key, RedisObject::createString(std::to_string(i)));
    }
    assert(ht.size() == static_cast<size_t>(N));

    // Drain rehashing to stabilize.
    for (int i = 0; i < 10000; ++i) ht.rehashStep(128);

    // Verify all keys are findable.
    for (int i = 0; i < N; ++i) {
        std::string key = "k" + std::to_string(i);
        HTEntry* entry = ht.find(key);
        assert(entry != nullptr);
        assert(entry->value.asString() == std::to_string(i));
    }

    // Delete even-numbered keys.
    for (int i = 0; i < N; i += 2) {
        std::string key = "k" + std::to_string(i);
        bool ok = ht.del(key);
        assert(ok);
    }
    assert(ht.size() == static_cast<size_t>(N / 2));

    // Odd keys must still exist.
    for (int i = 1; i < N; i += 2) {
        std::string key = "k" + std::to_string(i);
        HTEntry* entry = ht.find(key);
        assert(entry != nullptr);
    }
    // Even keys must be gone.
    for (int i = 0; i < N; i += 2) {
        std::string key = "k" + std::to_string(i);
        assert(ht.find(key) == nullptr);
    }
    check("large_scale", true);
}

// ── Test: Empty hash table operations ─────────────────────────────────
// Verifies that operations on an empty table are safe.
static void test_empty_table() {
    HashTable ht;
    assert(ht.size() == 0);
    assert(ht.find("anything") == nullptr);
    assert(!ht.del("anything"));
    assert(ht.keys().empty());
    check("empty_table", true);
}

// ── Test: ExpireAt field initialized to -1 ────────────────────────────
// Verifies INV-9: newly inserted entries have expireAt == -1.
static void test_expire_at_default() {
    HashTable ht;
    ht.set("key", RedisObject::createString("val"));
    HTEntry* entry = ht.find("key");
    assert(entry != nullptr);
    assert(entry->expireAt == -1);
    check("expire_at_default", true);
}

// ── Test: Integer encoding for numeric strings ────────────────────────
// Verifies RedisObject::createString uses INTEGER encoding for "12345".
static void test_integer_encoding() {
    auto obj = RedisObject::createString("12345");
    assert(obj.encoding == Encoding::INTEGER);
    assert(std::get<int64_t>(obj.data) == 12345);
    assert(obj.asString() == "12345");

    auto obj2 = RedisObject::createString("-42");
    assert(obj2.encoding == Encoding::INTEGER);
    assert(std::get<int64_t>(obj2.data) == -42);
    assert(obj2.asString() == "-42");

    auto obj3 = RedisObject::createString("hello");
    assert(obj3.encoding == Encoding::RAW);
    assert(std::get<std::string>(obj3.data) == "hello");
    assert(obj3.asString() == "hello");
    check("integer_encoding", true);
}

int main() {
    std::printf("=== HashTable Unit Tests ===\n");

    test_insert_and_find();
    test_find_nonexistent();
    test_overwrite();
    test_delete_existing();
    test_delete_nonexistent();
    test_keys_collection();
    test_rehash_triggers();
    test_large_scale();
    test_empty_table();
    test_expire_at_default();
    test_integer_encoding();

    std::printf("\n%d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
