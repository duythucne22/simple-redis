#pragma once

#include "store/RedisObject.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// An entry in the hash table's separate-chaining linked list.
struct HTEntry {
    std::string key;
    RedisObject value;
    uint64_t hashCode;          // cached hash — avoids rehashing during migration
    int64_t expireAt = -1;      // -1 = no expiry; milliseconds since epoch (Phase 3)
    HTEntry* next = nullptr;    // next entry in the chain
};

/// Primary key-value store. Separate chaining with FNV-1a hash.
/// Power-of-two sizing. Incremental rehashing via a dual-table approach.
///
/// During rehashing, two tables exist: primary_ (new, larger) and rehash_
/// (old, being drained). Reads check primary_ first, then rehash_. Writes
/// always go to primary_. Each mutating operation migrates up to
/// kRehashBatchSize entries from rehash_ to primary_.
///
/// Must NOT know about: RESP, commands, networking, TTL heap.
class HashTable {
public:
    HashTable();
    ~HashTable();

    HashTable(const HashTable&) = delete;
    HashTable& operator=(const HashTable&) = delete;

    /// Find an entry by key. Returns nullptr if not found.
    /// Checks primary_ first, then rehash_ (during rehashing).
    HTEntry* find(const std::string& key);

    /// Insert or overwrite a key-value pair. Always writes to primary_.
    void set(const std::string& key, RedisObject value);

    /// Delete a key. Returns true if the key existed.
    bool del(const std::string& key);

    /// Return the total number of entries across both tables.
    size_t size() const;

    /// Collect all keys from both tables.
    std::vector<std::string> keys() const;

    /// Perform up to nSteps incremental rehashing migrations.
    /// Called once per event loop tick to spread rehash cost.
    void rehashStep(int nSteps = 128);

private:
    /// Internal table structure — an array of linked-list heads.
    struct Table {
        HTEntry** slots = nullptr;  // array of linked-list heads
        size_t capacity = 0;        // always a power of 2
        size_t mask = 0;            // capacity - 1
        size_t size = 0;            // number of entries in this table
    };

    Table primary_;          // the current (or new, larger) table
    Table rehash_;           // the old table being drained during rehashing
    bool isRehashing_ = false;
    size_t rehashIdx_ = 0;   // next slot in rehash_ to migrate

    // Initial capacity — small, grows quickly via rehashing.
    static constexpr size_t kInitialCapacity = 4;
    // Trigger rehash when load factor exceeds this.
    static constexpr double kMaxLoadFactor = 2.0;
    // Number of entries to migrate per rehashStep() call.
    static constexpr int kRehashBatchSize = 128;

    /// FNV-1a 64-bit hash function.
    static uint64_t hash(const std::string& key);

    /// Allocate a new Table with the given capacity (must be power of 2).
    static Table allocTable(size_t capacity);

    /// Free a Table's slot array.
    static void freeTable(Table& table);

    /// Begin rehashing: move primary_ → rehash_, allocate new primary_.
    void triggerRehash();

    /// Migrate one slot from rehash_ to primary_.
    void migrateOneSlot();

    /// Find an entry in a specific table.
    static HTEntry* findInTable(Table& table, const std::string& key,
                                uint64_t hashCode);

    /// Delete an entry from a specific table. Returns true if found.
    static bool delFromTable(Table& table, const std::string& key,
                             uint64_t hashCode);
};
