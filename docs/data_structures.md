# Data Structures

This document covers the internal data structures used by simple-redis: the hash table that stores all keys, the skip list that orders sorted sets, the TTL heap that drives active expiry, the buffer that handles network I/O, and the `RedisObject` variant that unifies all value types.

---

## Hash Table

**File:** `src/store/HashTable.h` / `HashTable.cpp`

The primary key-value store. Every key in the database maps to an `HTEntry` in this table.

### Design

- **Collision resolution:** Separate chaining (linked lists per bucket).
- **Hash function:** FNV-1a 64-bit — fast, good distribution, no external dependency.
- **Sizing:** Power-of-two capacity. Slot index computed as `hashCode & mask` where `mask = capacity - 1`.
- **Initial capacity:** 4 slots. Grows quickly via incremental rehashing.

### HTEntry Layout

```cpp
struct HTEntry {
    std::string key;
    RedisObject value;
    uint64_t    hashCode;       // cached — avoids rehashing during migration
    int64_t     expireAt = -1;  // -1 = no TTL; milliseconds since epoch
    HTEntry*    next = nullptr; // chain pointer
};
```

Caching `hashCode` in the entry is critical for rehashing performance — when migrating entries to a new table, the hash does not need to be recomputed.

### Incremental Rehashing

When the load factor exceeds 2.0, rehashing begins:

1. The current `primary_` table is moved to `rehash_`.
2. A new `primary_` table is allocated with double the capacity.
3. `isRehashing_` is set to true and `rehashIdx_` starts at 0.

During rehashing:

- **Reads** check `primary_` first, then `rehash_`.
- **Writes** always go to `primary_`.
- **Each mutating operation** (`set`, `del`) triggers `rehashStep()`, which migrates up to 128 entries from `rehash_` to `primary_`.
- **Once per event loop tick**, `Database::rehashStep()` runs to make progress even during read-heavy workloads.

When `rehash_` is fully drained, `isRehashing_` is set to false and the old table's slot array is freed.

**Why incremental?** A full rehash would block the event loop for O(n) time. By spreading the migration across operations, no single call pays more than O(128) migration cost.

### FNV-1a Hash Function

```
hash = FNV_OFFSET_BASIS (14695981039346656037)
for each byte in key:
    hash ^= byte
    hash *= FNV_PRIME (1099511628211)
return hash
```

FNV-1a has excellent avalanche properties for short keys (typical Redis key sizes) and compiles to a tight loop with no branches.

### SCAN Implementation

`scan(cursor, count)` iterates the `primary_` table only (simplified — no reverse-bit cursor). It walks consecutive slots from `cursor`, collecting keys until `count` entries are gathered or the table is exhausted. Returns `(nextCursor, keys)` where `nextCursor = 0` means iteration is complete.

---

## Skip List

**File:** `src/store/Skiplist.h` / `Skiplist.cpp`

A probabilistic ordered data structure used by sorted sets. Provides O(log n) expected time for insert, delete, and find.

### Structure

```
Level 3:  header ──────────────────────────────► nil
Level 2:  header ──────── B ──────────────────► nil
Level 1:  header ── A ── B ──── D ────────────► nil
Level 0:  header ── A ── B ── C ── D ── E ──► nil
                                    ◄── backward pointers at level 0
```

Each node holds:
- `member` (string) and `score` (double).
- `forward[]` — one pointer per level.
- `backward` — previous node at level 0 (for reverse iteration).

### Ordering

Nodes are ordered by `(score ASC, member ASC lexicographic)`. This matches Redis behavior — when scores are equal, members are compared lexicographically.

### Random Level Generation

Each new node gets a random level drawn from a geometric distribution with probability p = 0.25:

```cpp
int randomLevel() {
    int level = 1;
    while (level < kMaxLevel && (rng_() % kBranchingFactor) == 0)
        level++;
    return level;
}
```

With p = 0.25, roughly 75% of nodes are level-1, 18.75% level-2, 4.7% level-3, etc. Maximum 32 levels — sufficient for datasets up to ~4^32 elements.

The PRNG is a per-instance `std::mt19937`, avoiding static mutable state.

### Operations

| Operation | Expected Time | Description |
|-----------|--------------|-------------|
| `insert(member, score)` | O(log n) | Insert a new node (caller ensures no duplicate) |
| `remove(member, score)` | O(log n) | Remove exact (member, score) pair |
| `find(member, score)` | O(log n) | Find exact (member, score) pair |
| `rangeByRank(start, stop)` | O(log n + k) | Return elements between ranks (0-based) |
| `size()` | O(1) | Element count |

### ZSet Integration

Each sorted set (`ZSetData`) pairs a `Skiplist` with an `std::unordered_map<string, double>`:

- **Skiplist** provides ordered access for ZRANGE, ZRANK, ZCOUNT.
- **Dict** provides O(1) ZSCORE lookups.
- Both are kept in sync: every ZADD/ZREM updates both.

---

## TTL Heap

**File:** `src/store/TTLHeap.h` / `TTLHeap.cpp`

A binary min-heap that tracks key expiration deadlines, enabling efficient active expiry.

### Design

```
heap_[0] = earliest deadline (smallest expireAtMs)
keyToIndex_ = { key → position in heap_ }
```

The `keyToIndex_` hash map enables O(1) key lookups within the heap, making `remove()` and `update()` O(log n) instead of O(n).

### Active Expiry Cycle

Every 100ms, the timer callback calls `Database::activeExpireCycle(200)`, which:

1. Calls `TTLHeap::popExpired(now, 200)`.
2. For each returned key, deletes it from the hash table.
3. Stops after 200 keys to avoid starving the event loop.

### Lazy Expiry

In addition to the heap-driven active cycle, every `Database::findEntry()` call checks the entry's `expireAt` field. If the entry is expired, it is deleted immediately. This ensures expired keys are never returned to clients, even if the active cycle hasn't reached them yet.

### Heap Operations

| Operation | Complexity |
|-----------|-----------|
| `push(key, expireAtMs)` | O(log n) — siftUp |
| `remove(key)` | O(log n) — swap with last, siftDown/siftUp |
| `update(key, newExpireAtMs)` | O(log n) — update in-place, restore heap property |
| `popExpired(nowMs, maxWork)` | O(k log n) — pop k expired entries |

---

## Buffer

**File:** `src/net/Buffer.h` / `Buffer.cpp`

A contiguous byte buffer optimized for the read-parse-write cycle of network I/O.

### Two-Cursor Layout

```
data_: [   consumed   |   readable   |   writable   ]
        0          readPos_       writePos_       capacity
```

- **Readable region:** `[readPos_, writePos_)` — unparsed incoming data or unsent outgoing data.
- **Writable region:** `[writePos_, capacity)` — space for `read()` syscalls or `append()`.
- **Consumed region:** `[0, readPos_)` — already processed, reclaimable.

### Compaction Strategy

| Tier | Condition | Action | Cost |
|------|-----------|--------|------|
| 1 | `readPos_ == writePos_` | Reset both to 0 | O(1) |
| 2 | Writable space insufficient, but front is free | `memmove` readable data to front | O(readable) |
| 3 | Still insufficient after compaction | Resize `std::vector` | O(capacity) |

This avoids `erase(begin, begin+n)` which would be O(n) for every `consume()` call.

### Memory Characteristics

- Initial capacity: 4096 bytes (matches typical TCP MSS).
- Idle connection memory: ~8 KB (one incoming + one outgoing buffer).
- Buffers grow on demand and do not shrink automatically.

---

## RedisObject Variant

**File:** `src/store/RedisObject.h` / `RedisObject.cpp`

The polymorphic value type that unifies all five Redis data types into a single `std::variant`.

### Variant Alternatives

```cpp
using RedisData = std::variant<
    std::string,                                   // STRING / RAW
    int64_t,                                       // STRING / INTEGER
    std::deque<std::string>,                       // LIST
    std::unordered_map<std::string, std::string>,  // HASH
    std::unordered_set<std::string>,               // SET
    ZSetData                                       // ZSET
>;
```

### Type + Encoding Tags

The `DataType` and `Encoding` enums add semantic meaning on top of the variant:

- `DataType::STRING` + `Encoding::INTEGER` → the variant holds `int64_t`. This enables efficient INCR/DECR without string parsing on every operation.
- `DataType::STRING` + `Encoding::RAW` → the variant holds `std::string`.
- `DataType::LIST` + `Encoding::LINKEDLIST` → `std::deque` (O(1) push/pop at both ends).
- `DataType::HASH` / `DataType::SET` + `Encoding::HASHTABLE` → `std::unordered_map` / `std::unordered_set`.
- `DataType::ZSET` + `Encoding::SKIPLIST` → `ZSetData` (Skiplist + dict).

### Memory Usage Estimation

`memoryUsage()` calculates the total memory consumed by a `RedisObject`:

```
Base cost: sizeof(RedisObject)  (~104 bytes)
+ variant-specific dynamic data:
  - STRING/RAW: string capacity
  - STRING/INTEGER: 0 (inline in int64_t)
  - LIST: count × (string overhead + avg element capacity)
  - HASH: bucket count × pointer + entry count × (2 strings)
  - SET: bucket count × pointer + entry count × string
  - ZSET: dict memory + skiplist node memory (3 pointers/level + string per node)
```

This is an estimate — exact allocator overhead varies. The running total is maintained in `Database::usedMemory_` and reported by `INFO memory`.
