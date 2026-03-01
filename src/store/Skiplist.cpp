#include "store/Skiplist.h"

#include <algorithm>
#include <cassert>

// ---------------------------------------------------------------------------
// Construction / destruction / move semantics
// ---------------------------------------------------------------------------

Skiplist::Skiplist()
    : header_(new Node("", 0, kMaxLevel)), rng_(std::random_device{}()) {}

Skiplist::~Skiplist() { deleteAllNodes(); }

Skiplist::Skiplist(Skiplist&& other) noexcept
    : header_(other.header_),
      level_(other.level_),
      size_(other.size_),
      rng_(std::move(other.rng_)) {
    other.header_ = nullptr;
    other.level_ = 1;
    other.size_ = 0;
}

Skiplist& Skiplist::operator=(Skiplist&& other) noexcept {
    if (this != &other) {
        deleteAllNodes();
        header_ = other.header_;
        level_ = other.level_;
        size_ = other.size_;
        rng_ = std::move(other.rng_);
        other.header_ = nullptr;
        other.level_ = 1;
        other.size_ = 0;
    }
    return *this;
}

// ---------------------------------------------------------------------------
// Core operations
// ---------------------------------------------------------------------------

Skiplist::Node* Skiplist::insert(const std::string& member, double score) {
    // update[i] = last node at level i whose successor is >= new node.
    Node* update[kMaxLevel];
    Node* x = header_;

    for (int i = level_ - 1; i >= 0; --i) {
        while (x->forward[i] &&
               lessThan(x->forward[i]->score, x->forward[i]->member,
                        score, member)) {
            x = x->forward[i];
        }
        update[i] = x;
    }

    int newLevel = randomLevel();
    if (newLevel > level_) {
        for (int i = level_; i < newLevel; ++i) {
            update[i] = header_;
        }
        level_ = newLevel;
    }

    Node* node = new Node(member, score, newLevel);

    // Splice into each level.
    for (int i = 0; i < newLevel; ++i) {
        node->forward[i] = update[i]->forward[i];
        update[i]->forward[i] = node;
    }

    // Set backward pointer (level 0 doubly linked).
    node->backward = (update[0] == header_) ? nullptr : update[0];
    if (node->forward[0]) {
        node->forward[0]->backward = node;
    }

    ++size_;
    return node;
}

bool Skiplist::remove(const std::string& member, double score) {
    Node* update[kMaxLevel];
    Node* x = header_;

    for (int i = level_ - 1; i >= 0; --i) {
        while (x->forward[i] &&
               lessThan(x->forward[i]->score, x->forward[i]->member,
                        score, member)) {
            x = x->forward[i];
        }
        update[i] = x;
    }

    x = x->forward[0];
    if (!x || x->score != score || x->member != member) {
        return false;  // not found
    }

    // Unlink from each level.
    for (int i = 0; i < level_; ++i) {
        if (update[i]->forward[i] != x) break;
        update[i]->forward[i] = x->forward[i];
    }

    // Fix backward pointer.
    if (x->forward[0]) {
        x->forward[0]->backward = x->backward;
    }

    delete x;
    --size_;

    // Shrink level if top levels are now empty.
    while (level_ > 1 && !header_->forward[level_ - 1]) {
        --level_;
    }
    return true;
}

Skiplist::Node* Skiplist::find(const std::string& member, double score) {
    Node* x = header_;
    for (int i = level_ - 1; i >= 0; --i) {
        while (x->forward[i] &&
               lessThan(x->forward[i]->score, x->forward[i]->member,
                        score, member)) {
            x = x->forward[i];
        }
    }

    x = x->forward[0];
    if (x && x->score == score && x->member == member) {
        return x;
    }
    return nullptr;
}

std::vector<std::pair<std::string, double>>
Skiplist::rangeByRank(int start, int stop) {
    int n = static_cast<int>(size_);
    // Convert negative indices.
    if (start < 0) start += n;
    if (stop < 0) stop += n;
    // Clamp.
    if (start < 0) start = 0;
    if (stop >= n) stop = n - 1;

    std::vector<std::pair<std::string, double>> result;
    if (start > stop || start >= n) return result;

    // Walk level 0 to reach the start rank.
    Node* x = header_->forward[0];
    for (int i = 0; i < start && x; ++i) {
        x = x->forward[0];
    }

    for (int i = start; i <= stop && x; ++i) {
        result.emplace_back(x->member, x->score);
        x = x->forward[0];
    }
    return result;
}

size_t Skiplist::size() const { return size_; }

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

int Skiplist::randomLevel() {
    int lvl = 1;
    std::uniform_int_distribution<int> dist(0, kBranchingFactor - 1);
    while (dist(rng_) == 0 && lvl < kMaxLevel) {
        ++lvl;
    }
    return lvl;
}

void Skiplist::deleteAllNodes() {
    if (!header_) return;
    Node* x = header_->forward[0];
    while (x) {
        Node* next = x->forward[0];
        delete x;
        x = next;
    }
    delete header_;
    header_ = nullptr;
}

bool Skiplist::lessThan(double s1, const std::string& m1,
                        double s2, const std::string& m2) {
    if (s1 != s2) return s1 < s2;
    return m1 < m2;
}
