#pragma once

#include "store/HashTable.h"
#include "store/TTLHeap.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/// Thin wrapper over HashTable that command handlers call.
/// Provides named operations (get, set, del, exists, keys).
/// Runs one rehash step per call to amortize rehashing cost.
///
/// Must NOT know about: RESP, networking, command names.
class Database {
public:
    /// Get the value for a key (STRING type only). Returns nullopt if
    /// not found, expired, or wrong type (non-STRING).
    std::optional<std::string> get(const std::string& key);

    /// Set a key to a string value (clears any existing TTL).
    void set(const std::string& key, const std::string& value);

    /// Delete a key. Returns true if the key existed.
    bool del(const std::string& key);

    /// Check if a key exists (and is not expired).
    bool exists(const std::string& key);

    /// Return all keys.
    std::vector<std::string> keys();

    /// Scan keys starting at `cursor`, returning up to `count` keys.
    /// If `pattern` is not "*", only keys matching the glob are returned.
    /// Returns (nextCursor, matchingKeys). nextCursor=0 means iteration done.
    std::pair<size_t, std::vector<std::string>> scan(size_t cursor,
                                                      size_t count,
                                                      const std::string& pattern);

    /// Return the total number of keys.
    size_t dbsize() const;

    /// Advance incremental rehashing — call once per event loop tick.
    void rehashStep();

    /// Set expiration on an existing key. expireAtMs = ms since epoch.
    /// Returns true if the key exists (and TTL was set), false otherwise.
    bool setExpire(const std::string& key, int64_t expireAtMs);

    /// Remove expiration from a key, making it permanent.
    void removeExpire(const std::string& key);

    /// Return remaining TTL in milliseconds. -1 = no TTL, -2 = key doesn't exist.
    int64_t ttl(const std::string& key);

    /// Proactively expire up to maxWork keys from the TTL heap.
    /// Called by the timer callback every 100ms.
    void activeExpireCycle(int maxWork);

    /// Look up a key and return its HTEntry* (with lazy expiry check).
    /// Returns nullptr if the key doesn't exist or is expired.
    /// Used by Phase 5 command handlers to access non-string types directly.
    HTEntry* findEntry(const std::string& key);

    /// Insert or overwrite a key with an arbitrary RedisObject.
    /// Does NOT clear TTL — caller manages TTL if needed.
    void setObject(const std::string& key, RedisObject obj);

    /// Return a mutable reference to the underlying hash table.
    /// Used by future phases (TTL, etc.) that need direct entry access.
    HashTable& table() { return table_; }

    /// Delete all keys. Clears hash table, TTL heap, and memory counter.
    void flushdb();

    /// Return estimated memory usage of all stored objects (bytes).
    size_t usedMemory() const { return usedMemory_; }

    /// Return the number of keys that have a TTL set.
    size_t expiryCount() const;

private:
    HashTable table_;
    TTLHeap ttlHeap_;
    size_t usedMemory_ = 0;  // running estimate — updated on set/del/flush

    /// Check if an entry is expired and delete it if so (lazy expiry).
    /// Returns true if the entry was expired and removed.
    bool checkAndExpire(const std::string& key, HTEntry* entry);
};
