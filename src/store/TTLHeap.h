#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

/// An entry in the TTL min-heap.
struct HeapEntry {
    std::string key;
    int64_t expireAtMs;  // milliseconds since Unix epoch
};

/// Binary min-heap tracking keys by expiration time.
///
/// heap_[0] always holds the entry with the earliest deadline.
/// keyToIndex_ provides O(1) key-to-position lookup, making
/// remove and update operations O(log n) instead of O(n).
///
/// Must NOT know about: networking, RESP, commands, the hash table.
class TTLHeap {
public:
    /// Add a key with an expiration deadline. If key already exists, updates it.
    void push(const std::string& key, int64_t expireAtMs);

    /// Remove a key from the heap. No-op if key doesn't exist.
    void remove(const std::string& key);

    /// Update a key's expiration. Equivalent to remove + push but avoids
    /// unnecessary allocation when the key is already present.
    void update(const std::string& key, int64_t newExpireAtMs);

    /// Pop and return keys that have expired (expireAt <= nowMs).
    /// Stops after maxWork entries to avoid starving the event loop.
    std::vector<std::string> popExpired(int64_t nowMs, int maxWork = 200);

    /// Returns true if the heap is empty.
    bool empty() const;

    /// Returns the number of entries in the heap.
    size_t size() const;

private:
    std::vector<HeapEntry> heap_;
    std::unordered_map<std::string, size_t> keyToIndex_;  // O(1) key→position

    /// Restore heap property upward from idx.
    void siftUp(size_t idx);

    /// Restore heap property downward from idx.
    void siftDown(size_t idx);

    /// Swap two entries and update keyToIndex_ for both.
    void swapEntries(size_t a, size_t b);
};
