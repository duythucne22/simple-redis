# Performance

This document covers the performance characteristics of simple-redis: benchmark results, the latency histogram, the slow log, and the design choices that impact throughput.

---

## Benchmark Results

Measured with `redis-benchmark` — 100,000 requests, 50 concurrent clients, 64-byte values.

### Core Throughput

| Command | ops/sec |
|---------|---------|
| PING | 55,804 |
| SET (64B value) | 52,002 |
| GET | 78,370 |
| LPUSH | 20,056 |
| RPUSH | 34,025 |
| LPOP | 35,026 |
| HSET | 39,872 |
| SADD | 41,085 |
| ZADD | 36,576 |

### Pipelining

| Command | ops/sec | Improvement |
|---------|---------|-------------|
| SET (pipeline=20) | 140,845 | 2.7× |
| GET (pipeline=20) | 523,560 | 6.7× |

Pipelining eliminates per-command round-trip latency. With 20 commands batched per network round-trip, GET throughput exceeds 500K ops/sec.

### Custom Commands

| Command | ops/sec |
|---------|---------|
| DEL | 14,993 |
| EXISTS | 19,157 |
| DBSIZE | 24,331 |
| PEXPIRE | 20,202 |
| PTTL | 27,397 |
| FLUSHDB | 3,788 |
| INFO | 229 |

INFO is intentionally slow — it formats a multi-line string response with process stats, memory info, and histogram data.

---

## Why Single-Threaded Works

The server processes all commands on a single thread (ADR-001). This is the same model Redis uses, and it works well because:

1. **No lock contention.** Every lock acquisition is a wasted CPU cycle. With one thread, data structures are always consistent without synchronization.
2. **No context switches.** Thread scheduling overhead is eliminated.
3. **Cache-friendly.** A single thread keeps data structures in L1/L2 cache.
4. **I/O bound, not CPU bound.** Most Redis workloads are network-bound. The bottleneck is TCP round-trip time, not command execution.
5. **Simplicity.** No race conditions, no deadlocks, no memory ordering concerns.

The single exception is AOF background rewrite, which `fork()`s a child process. The child operates on a copy-on-write snapshot and never modifies shared memory.

---

## Latency Histogram

The server tracks command execution time in a 6-bucket histogram (microsecond resolution):

| Bucket | Range | Typical commands |
|--------|-------|-----------------|
| 0 | < 100 µs | GET, SET, PING, most commands |
| 1 | 100–500 µs | Large LRANGE, HGETALL |
| 2 | 500 µs – 1 ms | KEYS on medium datasets |
| 3 | 1–10 ms | Large dataset scans |
| 4 | 10–100 ms | FLUSHDB on large datasets |
| 5 | ≥ 100 ms | Background operations |

After 1.26M commands in the benchmark run:

```
latency_histogram_us_lt100:    1,250,943  (99.2%)
latency_histogram_us_lt500:           69  (0.005%)
latency_histogram_us_lt1000:           6
latency_histogram_us_lt10000:     10,015  (0.8%)
latency_histogram_us_lt100000:         2
latency_histogram_us_gte100000:        1
```

Over 99% of commands complete in under 100 microseconds. The commands in the 1–10ms bucket are primarily FLUSHDB operations during benchmarking.

### Implementation

Latency is measured per-command in `main.cpp`:

```cpp
auto start = std::chrono::steady_clock::now();
commandTable.dispatch(db, conn, *cmd);
auto end = std::chrono::steady_clock::now();

int64_t durationUs = duration_cast<microseconds>(end - start).count();
metrics.recordLatency(durationUs);
```

`recordLatency()` increments the appropriate bucket via `latencyBucketIndex()`, which scans the threshold array:

```cpp
static constexpr int64_t kLatencyThresholds[] = {100, 500, 1000, 10000, 100000};
```

No heap allocation, no branching — a simple linear scan of 5 thresholds.

---

## Slow Log

Commands that exceed a configurable threshold (default: 10,000 µs = 10ms) are recorded in a circular buffer of 128 entries.

Each entry captures:

| Field | Description |
|-------|-------------|
| `id` | Monotonic counter (unique across server lifetime) |
| `timestampUs` | Wall-clock time (µs since epoch) |
| `durationUs` | Execution time in microseconds |
| `args` | First 6 arguments of the command |

The slow log is accessible via the `INFO stats` section, which reports `slowlog_len` — the number of entries currently stored.

### Design

- **Circular buffer:** Fixed-size array of 128 `SlowLogEntry` structs. When full, the oldest entry is overwritten.
- **No heap allocation on the hot path:** The entry struct is pre-allocated. Only the `args` vector is populated on a slow command.
- **Argument truncation:** At most 6 arguments are stored per entry, matching Redis's `SLOWLOG` behavior.

---

## Memory Tracking

The server maintains a running estimate of memory used by stored objects (`Database::usedMemory_`).

### How It Works

- On `set()` / `setObject()`: Add `newObject.memoryUsage()`, subtract old object's usage if overwriting.
- On `del()`: Subtract the deleted object's `memoryUsage()`.
- On `flushdb()`: Reset to 0.

`RedisObject::memoryUsage()` estimates total bytes:

```
sizeof(RedisObject) + variant-specific dynamic data
  STRING/RAW:     string.capacity()
  STRING/INTEGER: 0 (inline)
  LIST:           sum of element capacities + deque overhead
  HASH:           bucket_count × ptr + entries × (key + value sizes)
  SET:            bucket_count × ptr + entries × member sizes
  ZSET:           dict memory + skiplist node memory
```

This is reported in the `INFO memory` section as `used_memory`.

---

## Connection Handling

### File Descriptor Limits

On startup, the server raises `RLIMIT_NOFILE` to 65,536 (or falls back to the current hard limit). This supports up to ~10,000 concurrent idle connections — each connection uses ~8 KB (two 4 KB buffers).

### Epoll Configuration

- **Mode:** Level-triggered (not edge-triggered).
- **Max events per poll:** 128.
- **Timeout:** 100ms per `epoll_wait()` call.
- **Listener drain:** All pending `accept()` calls are processed in one event loop tick.

Level-triggered mode is simpler and less error-prone than edge-triggered. The trade-off is slightly more syscalls (epoll may report the same fd multiple times), but this is negligible compared to command processing cost.

---

## Optimization Opportunities

The current implementation prioritizes correctness and clarity. Several optimizations are possible for higher throughput:

1. **Edge-triggered epoll** — reduces epoll_ctl syscalls for active connections.
2. **io_uring** — batches read/write syscalls, reducing kernel transitions.
3. **Buffer pooling** — avoids per-connection allocation for idle connections.
4. **Integer-encoded short strings** — Redis stores short integers as shared objects to save memory.
5. **Ziplist/listpack encodings** — compact representations for small lists/hashes/sets.
6. **Multi-threaded I/O** — Redis 6+ uses I/O threads for read/write while keeping command execution single-threaded.

---

## Running Benchmarks

### Quick Benchmark

```bash
cd bench
./run_benchmark.sh [port]
```

Runs `redis-benchmark` for core commands and prints results.

### Full Evaluation

```bash
cd bench
./run_full_evaluation.sh [port]
```

Starts the server, runs all benchmarks (including custom commands), captures `INFO` output, and generates a report.

### Using redis-benchmark Directly

```bash
# Basic throughput
redis-benchmark -p 6379 -n 100000 -c 50 -q

# Pipelining
redis-benchmark -p 6379 -n 100000 -c 50 -P 20 -t set,get -q

# Custom command
redis-benchmark -p 6379 -n 100000 -c 50 -q DBSIZE
redis-benchmark -p 6379 -n 100000 -c 50 -q PEXPIRE __rand_int__ 60000
```
