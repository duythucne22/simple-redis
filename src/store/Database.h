#pragma once

#include "store/HashTable.h"

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
    /// Get the value for a key. Returns nullopt if not found or expired.
    std::optional<std::string> get(const std::string& key);

    /// Set a key to a string value.
    void set(const std::string& key, const std::string& value);

    /// Delete a key. Returns true if the key existed.
    bool del(const std::string& key);

    /// Check if a key exists (and is not expired).
    bool exists(const std::string& key);

    /// Return all keys.
    std::vector<std::string> keys();

    /// Return the total number of keys.
    size_t dbsize() const;

    /// Advance incremental rehashing — call once per event loop tick.
    void rehashStep();

    /// Return a mutable reference to the underlying hash table.
    /// Used by future phases (TTL, etc.) that need direct entry access.
    HashTable& table() { return table_; }

private:
    HashTable table_;

    /// Check if an entry is expired and delete it if so (lazy expiry).
    /// Returns true if the entry was expired and removed.
    bool checkAndExpire(const std::string& key, HTEntry* entry);
};
