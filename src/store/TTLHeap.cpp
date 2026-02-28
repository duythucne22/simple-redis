#include "store/TTLHeap.h"

#include <cassert>
#include <algorithm>  // std::swap

// ── Public API ────────────────────────────────────────────────────────────

void TTLHeap::push(const std::string& key, int64_t expireAtMs) {
    // INV-4: No duplicate keys. If key exists, delegate to update.
    auto it = keyToIndex_.find(key);
    if (it != keyToIndex_.end()) {
        update(key, expireAtMs);
        return;
    }

    // Append to the end of the heap and sift up.
    heap_.push_back({key, expireAtMs});
    size_t idx = heap_.size() - 1;
    keyToIndex_[key] = idx;
    siftUp(idx);

    // INV-3: heap and map sizes must match.
    assert(heap_.size() == keyToIndex_.size());
}

void TTLHeap::remove(const std::string& key) {
    auto it = keyToIndex_.find(key);
    if (it == keyToIndex_.end()) return;  // no-op if key not in heap

    size_t idx = it->second;
    size_t lastIdx = heap_.size() - 1;

    if (idx != lastIdx) {
        swapEntries(idx, lastIdx);
    }

    // Remove the last element (which is now the target key).
    keyToIndex_.erase(heap_.back().key);
    heap_.pop_back();

    // Restore heap property at idx if the heap is not empty and idx is valid.
    if (idx < heap_.size()) {
        siftDown(idx);
        siftUp(idx);
    }

    // INV-3: heap and map sizes must match.
    assert(heap_.size() == keyToIndex_.size());
}

void TTLHeap::update(const std::string& key, int64_t newExpireAtMs) {
    auto it = keyToIndex_.find(key);
    if (it == keyToIndex_.end()) {
        // Key not present — treat as a push.
        push(key, newExpireAtMs);
        return;
    }

    size_t idx = it->second;
    heap_[idx].expireAtMs = newExpireAtMs;

    // The new deadline may be earlier or later — sift both directions.
    // Only one of these will actually move the entry.
    siftUp(idx);
    siftDown(idx);

    // INV-3: heap and map sizes must match.
    assert(heap_.size() == keyToIndex_.size());
}

std::vector<std::string> TTLHeap::popExpired(int64_t nowMs, int maxWork) {
    std::vector<std::string> expired;
    int count = 0;

    // INV-8: Process at most maxWork entries per call.
    while (!heap_.empty() && count < maxWork) {
        // Peek at the earliest deadline.
        if (heap_[0].expireAtMs > nowMs) break;  // nothing else expired

        // Record the key before removing.
        std::string key = heap_[0].key;

        // Remove entry at index 0 using the same logic as remove().
        size_t lastIdx = heap_.size() - 1;
        if (lastIdx > 0) {
            swapEntries(0, lastIdx);
        }

        // Remove the last element (which is now the expired entry).
        keyToIndex_.erase(heap_.back().key);
        heap_.pop_back();

        // Restore heap property at root if heap is not empty.
        if (!heap_.empty()) {
            siftDown(0);
        }

        expired.push_back(std::move(key));
        ++count;
    }

    // INV-3: heap and map sizes must match.
    assert(heap_.size() == keyToIndex_.size());
    return expired;
}

bool TTLHeap::empty() const {
    return heap_.empty();
}

size_t TTLHeap::size() const {
    return heap_.size();
}

// ── Private helpers ───────────────────────────────────────────────────────

void TTLHeap::siftUp(size_t idx) {
    while (idx > 0) {
        size_t parent = (idx - 1) / 2;
        if (heap_[idx].expireAtMs >= heap_[parent].expireAtMs) break;
        swapEntries(idx, parent);
        idx = parent;
    }
}

void TTLHeap::siftDown(size_t idx) {
    while (true) {
        size_t left  = 2 * idx + 1;
        size_t right = 2 * idx + 2;
        size_t smallest = idx;

        if (left < heap_.size() &&
            heap_[left].expireAtMs < heap_[smallest].expireAtMs) {
            smallest = left;
        }
        if (right < heap_.size() &&
            heap_[right].expireAtMs < heap_[smallest].expireAtMs) {
            smallest = right;
        }
        if (smallest == idx) break;

        swapEntries(idx, smallest);
        idx = smallest;
    }
}

void TTLHeap::swapEntries(size_t a, size_t b) {
    std::swap(heap_[a], heap_[b]);
    // INV-2: keyToIndex_ must stay synchronized after every swap.
    keyToIndex_[heap_[a].key] = a;
    keyToIndex_[heap_[b].key] = b;
}
