#pragma once

#include <cstddef>
#include <random>
#include <string>
#include <utility>
#include <vector>

/// An ordered probabilistic data structure for sorted sets.
/// Provides O(log n) expected insert, delete, and range-by-rank queries.
/// Ordered by (score ASC, member ASC lexicographic) — matches Redis behavior.
///
/// Used as the ordered index in ZSet alongside an unordered_map for O(1) ZSCORE.
///
/// Must NOT know about: TCP, RESP, commands, hash table, TTL, AOF.
class Skiplist {
public:
    /// A node in the skiplist. Each node holds a member-score pair and
    /// forward pointers (one per level) plus a backward pointer for
    /// reverse iteration at level 0.
    struct Node {
        std::string member;
        double score;
        std::vector<Node*> forward;   // one pointer per level
        Node* backward = nullptr;     // previous node at level 0

        Node(const std::string& m, double s, int level)
            : member(m), score(s), forward(level, nullptr) {}
    };

    Skiplist();
    ~Skiplist();

    // Move-only (non-copyable). Required for std::variant in RedisObject.
    Skiplist(Skiplist&& other) noexcept;
    Skiplist& operator=(Skiplist&& other) noexcept;
    Skiplist(const Skiplist&) = delete;
    Skiplist& operator=(const Skiplist&) = delete;

    /// Insert a new node with (member, score). Returns the new node.
    /// Caller must ensure no duplicate (member, score) exists.
    Node* insert(const std::string& member, double score);

    /// Remove the node with exact (member, score). Returns true if found.
    bool remove(const std::string& member, double score);

    /// Find the node with exact (member, score). Returns nullptr if not found.
    Node* find(const std::string& member, double score);

    /// Return elements between rank start and stop (inclusive, 0-based).
    /// Negative indices count from the end (-1 = last).
    /// Walks level 0 — O(n) rank lookup (simplified, no span tracking).
    std::vector<std::pair<std::string, double>> rangeByRank(int start, int stop);

    /// Return the number of elements.
    size_t size() const;

private:
    Node* header_;          // sentinel node — never holds real data
    int level_ = 1;         // current max level in use (1-based)
    size_t size_ = 0;       // number of real elements
    std::mt19937 rng_;      // per-instance PRNG — no static mutable state

    // Maximum possible level. With p=0.25, level 32 requires ~4^32 elements.
    static constexpr int kMaxLevel = 32;

    // Promotion probability: 25% chance per level (Redis default).
    static constexpr int kBranchingFactor = 4;  // 1/4 = 0.25

    /// Generate a random level for a new node (geometric distribution, p=0.25).
    int randomLevel();

    /// Delete all nodes (including header). Called by destructor and move-assign.
    void deleteAllNodes();

    /// Compare two (score, member) pairs. Returns true if (s1,m1) < (s2,m2).
    static bool lessThan(double s1, const std::string& m1,
                         double s2, const std::string& m2);
};
