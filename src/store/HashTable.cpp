#include "store/HashTable.h"

#include <cassert>
#include <cstring>

// ── FNV-1a 64-bit hash ────────────────────────────────────────────────────
// FNV offset basis and prime for 64-bit FNV-1a.
static constexpr uint64_t kFNVOffsetBasis = 14695981039346656037ULL;
static constexpr uint64_t kFNVPrime       = 1099511628211ULL;

uint64_t HashTable::hash(const std::string& key) {
    uint64_t h = kFNVOffsetBasis;
    for (unsigned char c : key) {
        h ^= c;
        h *= kFNVPrime;
    }
    return h;
}

// ── Table helpers ──────────────────────────────────────────────────────────

HashTable::Table HashTable::allocTable(size_t capacity) {
    // INV: capacity must be a power of 2.
    assert(capacity > 0 && (capacity & (capacity - 1)) == 0);
    Table t;
    t.slots    = new HTEntry*[capacity]();  // zero-initialized (all nullptr)
    t.capacity = capacity;
    t.mask     = capacity - 1;
    t.size     = 0;
    return t;
}

void HashTable::freeTable(Table& table) {
    if (table.slots) {
        // Free all entries in all chains.
        for (size_t i = 0; i < table.capacity; ++i) {
            HTEntry* entry = table.slots[i];
            while (entry) {
                HTEntry* next = entry->next;
                delete entry;
                entry = next;
            }
        }
        delete[] table.slots;
        table.slots    = nullptr;
        table.capacity = 0;
        table.mask     = 0;
        table.size     = 0;
    }
}

// ── Constructor / Destructor ──────────────────────────────────────────────

HashTable::HashTable()
    : primary_{}, rehash_{}, isRehashing_(false), rehashIdx_(0) {
    // primary_ starts as an empty table — allocated lazily on first set().
}

HashTable::~HashTable() {
    freeTable(primary_);
    freeTable(rehash_);
}

// ── Lookup ────────────────────────────────────────────────────────────────

HTEntry* HashTable::findInTable(Table& table, const std::string& key,
                                uint64_t hashCode) {
    if (table.slots == nullptr) return nullptr;
    size_t idx = hashCode & table.mask;
    HTEntry* entry = table.slots[idx];
    while (entry) {
        if (entry->hashCode == hashCode && entry->key == key) {
            return entry;
        }
        entry = entry->next;
    }
    return nullptr;
}

HTEntry* HashTable::find(const std::string& key) {
    uint64_t h = hash(key);

    // Check primary_ first (newer/larger table).
    HTEntry* entry = findInTable(primary_, key, h);
    if (entry) return entry;

    // During rehashing, also check the old table.
    if (isRehashing_) {
        entry = findInTable(rehash_, key, h);
    }
    return entry;
}

// ── Insert / Overwrite ────────────────────────────────────────────────────

void HashTable::set(const std::string& key, RedisObject value) {
    // Do incremental rehashing work if in progress.
    if (isRehashing_) {
        rehashStep(kRehashBatchSize);
    }

    uint64_t h = hash(key);

    // If rehashing, check and remove from the old table first.
    if (isRehashing_) {
        delFromTable(rehash_, key, h);
    }

    // Lazy allocation of primary_ on first insert.
    if (primary_.slots == nullptr) {
        primary_ = allocTable(kInitialCapacity);
    }

    // Check if key already exists in primary_ — overwrite if so.
    HTEntry* existing = findInTable(primary_, key, h);
    if (existing) {
        existing->value = std::move(value);
        // Preserve existing expireAt — the SET command will handle
        // resetting it if needed.
        return;
    }

    // Insert new entry at the head of the chain.
    auto* entry     = new HTEntry();
    entry->key      = key;
    entry->value    = std::move(value);
    entry->hashCode = h;
    entry->expireAt = -1;

    size_t idx = h & primary_.mask;
    entry->next          = primary_.slots[idx];
    primary_.slots[idx]  = entry;
    primary_.size++;

    // Check load factor — trigger rehash if needed.
    double loadFactor = static_cast<double>(primary_.size) /
                        static_cast<double>(primary_.capacity);
    if (!isRehashing_ && loadFactor > kMaxLoadFactor) {
        triggerRehash();
    }
}

// ── Delete ────────────────────────────────────────────────────────────────

bool HashTable::delFromTable(Table& table, const std::string& key,
                             uint64_t hashCode) {
    if (table.slots == nullptr) return false;
    size_t idx = hashCode & table.mask;
    HTEntry* prev  = nullptr;
    HTEntry* entry = table.slots[idx];
    while (entry) {
        if (entry->hashCode == hashCode && entry->key == key) {
            // Unlink from chain.
            if (prev) {
                prev->next = entry->next;
            } else {
                table.slots[idx] = entry->next;
            }
            delete entry;
            table.size--;
            return true;
        }
        prev  = entry;
        entry = entry->next;
    }
    return false;
}

bool HashTable::del(const std::string& key) {
    // Do incremental rehashing work if in progress.
    if (isRehashing_) {
        rehashStep(kRehashBatchSize);
    }

    uint64_t h = hash(key);

    // Try primary_ first.
    if (delFromTable(primary_, key, h)) return true;

    // During rehashing, also try the old table.
    if (isRehashing_) {
        return delFromTable(rehash_, key, h);
    }
    return false;
}

// ── Size ──────────────────────────────────────────────────────────────────

size_t HashTable::size() const {
    return primary_.size + rehash_.size;
}

// ── Keys ──────────────────────────────────────────────────────────────────

std::vector<std::string> HashTable::keys() const {
    std::vector<std::string> result;
    result.reserve(size());

    auto collect = [&](const Table& table) {
        if (!table.slots) return;
        for (size_t i = 0; i < table.capacity; ++i) {
            HTEntry* entry = table.slots[i];
            while (entry) {
                result.push_back(entry->key);
                entry = entry->next;
            }
        }
    };

    collect(primary_);
    collect(rehash_);
    return result;
}

// ── Incremental Rehashing ─────────────────────────────────────────────────

void HashTable::triggerRehash() {
    assert(!isRehashing_);
    // INV: primary_ capacity must be > 0 to trigger rehash.
    assert(primary_.slots != nullptr && primary_.capacity > 0);

    // Move current primary_ into rehash_ (it becomes the old table).
    rehash_    = primary_;
    // Allocate a new primary_ at 2× capacity.
    primary_   = allocTable(rehash_.capacity * 2);
    isRehashing_ = true;
    rehashIdx_   = 0;
}

void HashTable::migrateOneSlot() {
    // Skip empty slots.
    while (rehashIdx_ < rehash_.capacity &&
           rehash_.slots[rehashIdx_] == nullptr) {
        rehashIdx_++;
    }
    if (rehashIdx_ >= rehash_.capacity) {
        // All slots migrated — rehashing complete.
        freeTable(rehash_);
        isRehashing_ = false;
        rehashIdx_   = 0;
        return;
    }

    // Migrate all entries in this slot to primary_.
    HTEntry* entry = rehash_.slots[rehashIdx_];
    while (entry) {
        HTEntry* next = entry->next;

        // Re-insert into primary_ using the cached hashCode.
        size_t idx      = entry->hashCode & primary_.mask;
        entry->next     = primary_.slots[idx];
        primary_.slots[idx] = entry;
        primary_.size++;
        rehash_.size--;

        entry = next;
    }
    rehash_.slots[rehashIdx_] = nullptr;
    rehashIdx_++;

    // Check if rehashing is complete.
    if (rehash_.size == 0) {
        delete[] rehash_.slots;
        rehash_.slots    = nullptr;
        rehash_.capacity = 0;
        rehash_.mask     = 0;
        isRehashing_     = false;
        rehashIdx_       = 0;
    }
}

void HashTable::rehashStep(int nSteps) {
    if (!isRehashing_) return;
    for (int i = 0; i < nSteps && isRehashing_; ++i) {
        migrateOneSlot();
    }
}
