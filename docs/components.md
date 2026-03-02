# Components

This document describes every component in simple-redis, grouped by architectural layer. Each section covers the class's purpose, public API, internal design, and notable implementation details.

---

## Layer 0 — Store

### `RedisObject` (`store/RedisObject.h`)

The polymorphic value type stored for every key. Every entry in the hash table holds one `RedisObject`.

**Type system.** Two enum tags describe the value:

| `DataType` | `Encoding` | `RedisData` variant | Example |
|------------|------------|---------------------|---------|
| STRING | RAW | `std::string` | `"hello"` |
| STRING | INTEGER | `int64_t` | `42` |
| LIST | LINKEDLIST | `std::deque<std::string>` | LPUSH/RPUSH |
| HASH | HASHTABLE | `std::unordered_map<string,string>` | HSET |
| SET | HASHTABLE | `std::unordered_set<string>` | SADD |
| ZSET | SKIPLIST | `ZSetData` (Skiplist + dict) | ZADD |

**Factory methods.** `createString()`, `createList()`, `createHash()`, `createSet()`, `createZSet()` construct objects with correct type/encoding tags.

**Memory estimation.** `memoryUsage()` walks the active variant and sums `sizeof(RedisObject)` plus dynamic allocation estimates. Used by `Database` to maintain a running memory counter for the `INFO` command.

**Move-only.** `RedisObject` is non-copyable because `ZSetData` contains a `Skiplist` (which manages raw pointers). Move construction and assignment are defaulted.

---

### `HashTable` (`store/HashTable.h`)

Primary key-value store. Separate-chaining hash table with power-of-two sizing, FNV-1a hashing, and incremental rehashing.

**Dual-table rehashing.** When the load factor exceeds 2.0, the current table is moved to a `rehash_` slot and a new, double-sized `primary_` table is allocated. Each mutating operation (`set`, `del`) migrates up to `kRehashBatchSize` (128) entries from `rehash_` to `primary_`. Reads check `primary_` first, then `rehash_`.

**Key API:**

| Method | Complexity | Description |
|--------|-----------|-------------|
| `find(key)` | O(1) avg | Lookup by key, returns `HTEntry*` or nullptr |
| `set(key, value)` | O(1) amortized | Insert or overwrite |
| `del(key)` | O(1) avg | Delete, returns true if existed |
| `size()` | O(1) | Total entries across both tables |
| `keys()` | O(n) | Collect all keys into a vector |
| `scan(cursor, count)` | O(count) | Cursor-based iteration |
| `rehashStep(n)` | O(n) | Migrate up to n entries |
| `flushAll()` | O(n) | Delete all entries |
| `expiryCount()` | O(n) | Count entries with TTL set |

**`HTEntry` layout.** Each entry holds: `key` (string), `value` (RedisObject), `hashCode` (cached, avoids rehashing during migration), `expireAt` (millisecond timestamp, -1 = no expiry), and `next` (chain pointer).

---

### `Database` (`store/Database.h`)

Thin facade over `HashTable` and `TTLHeap`. This is the only store-layer component that command handlers interact with.

**Responsibilities:**

- **Named operations:** `get()`, `set()`, `del()`, `exists()`, `keys()`, `scan()`, `dbsize()`.
- **Lazy expiry:** Every `findEntry()` call checks the entry's `expireAt` and deletes it if expired.
- **Active expiry:** `activeExpireCycle(maxWork)` pops expired keys from the TTL heap (called every 100ms by the timer).
- **TTL management:** `setExpire()`, `removeExpire()`, `ttl()`.
- **Memory tracking:** Maintains a running `usedMemory_` counter, updated on every `set()`, `del()`, and `flushdb()`.
- **Rehash forwarding:** `rehashStep()` delegates to `HashTable::rehashStep()`, called once per event loop tick.
- **Direct access:** `findEntry()` and `setObject()` let command handlers work with non-string types (lists, hashes, sets, sorted sets) directly via `HTEntry*`.

---

### `TTLHeap` (`store/TTLHeap.h`)

Binary min-heap tracking key expiration deadlines.

`heap_[0]` always holds the entry with the earliest deadline. A `keyToIndex_` hash map provides O(1) key-to-position lookup, making `remove()` and `update()` O(log n).

| Method | Complexity | Description |
|--------|-----------|-------------|
| `push(key, expireAtMs)` | O(log n) | Add or update a key's deadline |
| `remove(key)` | O(log n) | Remove a key from the heap |
| `update(key, newExpireAtMs)` | O(log n) | Update deadline in-place |
| `popExpired(nowMs, maxWork)` | O(k log n) | Pop up to k expired keys |

---

### `Skiplist` (`store/Skiplist.h`)

Probabilistic ordered data structure for sorted sets. Provides O(log n) expected time for insert, delete, and find. Nodes are ordered by `(score ASC, member ASC)`, matching Redis behavior.

**Design details:**

- Maximum 32 levels with promotion probability p = 0.25 (branching factor 4).
- Per-instance `std::mt19937` PRNG — no static mutable state.
- Backward pointers at level 0 support reverse iteration.
- `rangeByRank(start, stop)` walks level 0 to find elements by zero-based rank (simplified — no span tracking).

---

## Layer 1 — Network

### `Buffer` (`net/Buffer.h`)

Contiguous byte buffer for network I/O. Uses a two-cursor design (`readPos_`, `writePos_`) backed by a single `std::vector<uint8_t>`.

**Three-tier compaction strategy:**

1. **Tier 1 — Reset on empty.** When all bytes are consumed, reset both cursors to 0. Cost: O(1).
2. **Tier 2 — Compact.** When writable space at the back is insufficient but total free space (front + back) suffices, `memmove` readable data to the front. Cost: O(readable bytes).
3. **Tier 3 — Grow.** When compaction is not enough, resize the underlying vector.

Initial capacity is 4096 bytes (matches typical MTU and keeps idle-connection memory low).

---

### `Connection` (`net/Connection.h`)

Wraps a client file descriptor and owns its `incoming` and `outgoing` `Buffer` objects. Held via `unique_ptr` — not copyable, not movable.

**I/O methods:** `handleRead()` reads from the fd into `incoming` (returns false on EOF/error). `handleWrite()` writes from `outgoing` to the fd (returns false on error).

**State flags:** `wantRead_`, `wantWrite_`, `wantClose_` — used by `main.cpp` to update epoll registration.

**Transaction state:** `std::optional<TransactionState> txn` — when `has_value()`, the connection is in `MULTI` mode. Queued commands are stored as `vector<vector<string>>`.

**Pub/Sub state:** `subscribedChannels` (unordered_set) tracks which channels this connection is subscribed to. `inSubscribeMode()` returns true when the set is non-empty, causing `main.cpp` to gate commands.

---

### `Listener` (`net/Listener.h`)

Manages the server's listening socket. Binds to a given address:port with `SO_REUSEADDR`, sets non-blocking mode, and calls `listen()`. `acceptClient()` returns a non-blocking client fd or -1 on `EAGAIN`.

---

### `EventLoop` (`net/EventLoop.h`)

Owns the `epoll` instance. Provides `addFd()`, `modFd()`, `removeFd()` for fd registration, and `poll(timeoutMs)` for one iteration of `epoll_wait`. A configurable timer callback fires when the configured interval elapses (checked after each `poll()`).

Maximum concurrent events per poll: 128 (`kMaxEvents`).

---

## Layer 2 — Protocol

### `RespParser` (`proto/RespParser.h`)

Parses RESP2 commands from a `Buffer`. Supports two formats:

1. **RESP arrays:** `*N\r\n$len\r\narg\r\n...` — the standard binary-safe format used by `redis-cli` and client libraries.
2. **Inline commands:** `COMMAND arg1 arg2\r\n` — text-based, split on spaces.

If the buffer does not contain a complete frame, `parse()` returns `std::nullopt` and leaves the buffer untouched (zero-copy on incomplete data). Only consumes bytes on a successful parse.

---

### `RespSerializer` (`proto/RespSerializer.h`)

Writes RESP2 response tokens into an outgoing `Buffer`. All methods are static — no internal state.

| Method | Wire format |
|--------|-------------|
| `writeSimpleString(buf, s)` | `+s\r\n` |
| `writeError(buf, msg)` | `-msg\r\n` |
| `writeInteger(buf, val)` | `:val\r\n` |
| `writeBulkString(buf, s)` | `$len\r\ndata\r\n` |
| `writeNull(buf)` | `$-1\r\n` |
| `writeArrayHeader(buf, count)` | `*count\r\n` |

---

## Layer 3 — Commands

### `CommandTable` (`cmd/CommandTable.h`)

Central dispatch table. Maps command names (uppercased) to `CommandEntry` structs containing the handler function, arity, and write flag. Dispatch flow:

1. Uppercase the command name.
2. Look up in the hash map — O(1).
3. Validate arity (positive = exact, negative = minimum).
4. Call the handler with `(Database&, Connection&, args)`.

### `StringCommands` (`cmd/StringCommands.h`)

Registers: **SET**, **GET**, **PING**, **ECHO**, **APPEND**, **INCR**, **DECR**, **INCRBY**, **DECRBY**, **MGET**, **MSET**.

- SET supports `EX`, `PX`, `NX`, `XX` options.
- INCR/DECR operate on integer-encoded strings. Return an error for non-integer values.

### `KeyCommands` (`cmd/KeyCommands.h`)

Registers: **DEL**, **EXISTS**, **KEYS**, **RENAME**, **TYPE**, **SCAN**.

- DEL accepts multiple keys and returns the count of deleted keys.
- SCAN implements cursor-based iteration with optional MATCH and COUNT.

### `ListCommands` (`cmd/ListCommands.h`)

Registers: **LPUSH**, **RPUSH**, **LPOP**, **RPOP**, **LRANGE**, **LLEN**, **LINDEX**, **LSET**.

Lists are backed by `std::deque<std::string>`, giving O(1) push/pop at both ends.

### `HashCommands` (`cmd/HashCommands.h`)

Registers: **HSET**, **HGET**, **HDEL**, **HGETALL**, **HLEN**, **HEXISTS**, **HKEYS**, **HVALS**.

Backed by `std::unordered_map<std::string, std::string>`.

### `SetCommands` (`cmd/SetCommands.h`)

Registers: **SADD**, **SREM**, **SISMEMBER**, **SMEMBERS**, **SCARD**, **SINTER**, **SUNION**.

Backed by `std::unordered_set<std::string>`.

### `ZSetCommands` (`cmd/ZSetCommands.h`)

Registers: **ZADD**, **ZREM**, **ZSCORE**, **ZRANK**, **ZRANGE** (with WITHSCORES), **ZCARD**, **ZCOUNT**.

Each sorted set stores a `ZSetData` containing a `Skiplist` (for ordered access) and an `std::unordered_map<string, double>` (for O(1) score lookups). Both structures are kept in sync on every mutation.

### `TransactionCommands` (`cmd/TransactionCommands.h`)

Registers: **MULTI**, **DISCARD**.

MULTI sets `conn.txn` to an empty `TransactionState`. Subsequent commands are queued (not executed) until EXEC or DISCARD. EXEC is registered in `main.cpp` because it needs access to `CommandTable` and `AOFWriter` for re-dispatch and AOF logging.

### `PubSubCommands` (`cmd/PubSubCommands.h`)

Provides helper functions, but **SUBSCRIBE**, **UNSUBSCRIBE**, and **PUBLISH** are registered in `main.cpp` via lambda captures over `PubSubRegistry`.

### `PubSubRegistry` (`cmd/PubSubRegistry.h`)

Maintains a `channel → set<Connection*>` mapping. `publish()` writes the RESP push message directly into each subscriber's outgoing buffer. `removeConnection()` cleans up all subscriptions when a client disconnects.

### `ServerCommands` (`cmd/ServerCommands.h`)

Registers: **INFO**, **DBSIZE**, **FLUSHDB**.

- **INFO** returns a multi-section response (Server, Clients, Memory, Stats, Keyspace) including latency histogram and slow log length.
- **DBSIZE** returns the key count.
- **FLUSHDB** deletes all keys and resets memory tracking.

Depends on `ServerMetrics`, a struct defined in the same header that tracks `totalCommandsProcessed`, a 6-bucket latency histogram, and a 128-entry circular slow log.

---

## Persistence Overlay

### `AOFWriter` (`persistence/AOFWriter.h`)

Appends every write command to `appendonly.aof` in RESP format.

**Fsync policies:**

| Policy | Behavior | Durability |
|--------|----------|-----------|
| ALWAYS | `fsync()` after every `log()` | Best — at most 1 command lost |
| EVERYSEC | `fsync()` once per second via `tick()` | ≤ 1 second of data loss |
| NO | No explicit fsync — OS decides | Least durable, highest throughput |

**Background rewrite:**

1. `triggerRewrite()` calls `fork()`.
2. The child process iterates the database snapshot and writes a minimal AOF (one command per key).
3. The parent continues logging new commands to both the old file and a `rewriteBuffer_`.
4. `checkRewriteComplete()` (called every 100ms) waits on the child via `waitpid(WNOHANG)`.
5. On child completion, the parent appends the rewrite buffer to the new file and atomically renames it.

### `AOFLoader` (`persistence/AOFLoader.h`)

Replays the AOF file on startup. Uses `RespParser` to parse commands from the file and `CommandTable::dispatch()` to execute them against the database. Handles truncated files gracefully — loads the valid prefix and logs a warning.
