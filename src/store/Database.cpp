#include "store/Database.h"

#include <chrono>

/// Return current time in milliseconds since epoch.
static int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
               system_clock::now().time_since_epoch())
        .count();
}

bool Database::checkAndExpire(const std::string& key, HTEntry* entry) {
    if (entry->expireAt < 0) return false;  // no expiry set
    if (nowMs() < entry->expireAt) return false;  // not yet expired
    // Subtract memory before deletion.
    usedMemory_ -= entry->value.memoryUsage();
    // INV-7: Remove from heap when lazy-expiring a key.
    ttlHeap_.remove(key);
    table_.del(key);
    return true;
}

std::optional<std::string> Database::get(const std::string& key) {
    table_.rehashStep();

    HTEntry* entry = table_.find(key);
    if (!entry) return std::nullopt;

    // Lazy expiry: check if the key has expired.
    if (checkAndExpire(key, entry)) return std::nullopt;

    // Phase 5: only STRING type is returned via get().
    if (entry->value.type != DataType::STRING) return std::nullopt;

    return entry->value.asString();
}

void Database::set(const std::string& key, const std::string& value) {
    // INV-6: SET clears any existing TTL on the key.
    ttlHeap_.remove(key);

    // Subtract old memory if key already exists.
    HTEntry* old = table_.find(key);
    if (old) usedMemory_ -= old->value.memoryUsage();

    // If the key already exists in the hash table, we need to reset expireAt.
    // After table_.set(), find the entry and ensure expireAt = -1.
    table_.set(key, RedisObject::createString(value));

    // Ensure expireAt is cleared (table_.set overwrite preserves expireAt).
    HTEntry* entry = table_.find(key);
    if (entry) {
        entry->expireAt = -1;
        usedMemory_ += entry->value.memoryUsage();
    }
}

bool Database::del(const std::string& key) {
    // Subtract memory before deletion.
    HTEntry* entry = table_.find(key);
    if (entry) usedMemory_ -= entry->value.memoryUsage();
    // INV-5: Remove from heap when a key is DEL'd.
    ttlHeap_.remove(key);
    return table_.del(key);
}

bool Database::exists(const std::string& key) {
    table_.rehashStep();

    HTEntry* entry = table_.find(key);
    if (!entry) return false;

    // Lazy expiry check.
    if (checkAndExpire(key, entry)) return false;

    return true;
}

std::vector<std::string> Database::keys() {
    table_.rehashStep();
    return table_.keys();
}

std::pair<size_t, std::vector<std::string>>
Database::scan(size_t cursor, size_t count, const std::string& pattern) {
    table_.rehashStep();

    auto [nextCursor, rawKeys] = table_.scan(cursor, count);

    // Filter by pattern (only "*" or exact match supported).
    // Also filter out expired keys via lazy expiry.
    std::vector<std::string> filteredKeys;
    for (auto& key : rawKeys) {
        // Lazy expiry check.
        HTEntry* entry = table_.find(key);
        if (!entry) continue;
        if (checkAndExpire(key, entry)) continue;

        // Pattern match: "*" matches everything.
        if (pattern == "*") {
            filteredKeys.push_back(std::move(key));
        } else if (key == pattern) {
            // Simple exact match fallback (real Redis uses glob matching).
            filteredKeys.push_back(std::move(key));
        }
    }

    return {nextCursor, filteredKeys};
}

size_t Database::dbsize() const {
    return table_.size();
}

void Database::rehashStep() {
    table_.rehashStep();
}

bool Database::setExpire(const std::string& key, int64_t expireAtMs) {
    HTEntry* entry = table_.find(key);
    if (!entry) return false;

    // Lazy check: if the key is already expired, don't set a new TTL.
    if (checkAndExpire(key, entry)) return false;

    entry->expireAt = expireAtMs;
    ttlHeap_.push(key, expireAtMs);
    return true;
}

void Database::removeExpire(const std::string& key) {
    HTEntry* entry = table_.find(key);
    if (!entry) return;

    entry->expireAt = -1;
    ttlHeap_.remove(key);
}

int64_t Database::ttl(const std::string& key) {
    HTEntry* entry = table_.find(key);
    if (!entry) return -2;  // key doesn't exist

    // Lazy expiry check.
    if (entry->expireAt >= 0 && nowMs() >= entry->expireAt) {
        // Key is expired — clean up and report as non-existent.
        ttlHeap_.remove(key);
        table_.del(key);
        return -2;
    }

    if (entry->expireAt < 0) return -1;  // no TTL set
    return entry->expireAt - nowMs();     // remaining time in ms
}

void Database::activeExpireCycle(int maxWork) {
    int64_t now = nowMs();
    auto expired = ttlHeap_.popExpired(now, maxWork);
    for (const auto& key : expired) {
        // Subtract memory before deletion.
        HTEntry* entry = table_.find(key);
        if (entry) usedMemory_ -= entry->value.memoryUsage();
        // The heap entry is already removed by popExpired.
        table_.del(key);
    }
}

HTEntry* Database::findEntry(const std::string& key) {
    table_.rehashStep();

    HTEntry* entry = table_.find(key);
    if (!entry) return nullptr;

    // Lazy expiry check.
    if (checkAndExpire(key, entry)) return nullptr;

    return entry;
}

void Database::setObject(const std::string& key, RedisObject obj) {
    // Subtract old memory if key already exists.
    HTEntry* old = table_.find(key);
    if (old) usedMemory_ -= old->value.memoryUsage();

    table_.set(key, std::move(obj));

    // Add new memory.
    HTEntry* entry = table_.find(key);
    if (entry) usedMemory_ += entry->value.memoryUsage();
}

void Database::flushdb() {
    table_.flushAll();
    ttlHeap_ = TTLHeap{};  // reset heap
    usedMemory_ = 0;
}

size_t Database::expiryCount() const {
    return table_.expiryCount();
}
