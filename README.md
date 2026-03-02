# simple-redis

![Language](https://img.shields.io/badge/language-C%2B%2B17-00599C)
![Platform](https://img.shields.io/badge/platform-Linux%20(epoll)-FCC624)
![Protocol](https://img.shields.io/badge/protocol-RESP2-2EA44F)
![Build](https://img.shields.io/badge/build-Makefile-6E7781)

A Redis-compatible server written from scratch in C++17. Single-threaded, event-driven, and built on Linux epoll — no external dependencies beyond the C++ standard library and POSIX.

## Features

- **RESP2 protocol** — works with `redis-cli`, `redis-benchmark`, and standard client libraries
- **5 data types** — strings, lists, hashes, sets, sorted sets
- **35 commands** — core Redis operations across all data types
- **TTL & expiry** — millisecond-precision with lazy + active expiry
- **AOF persistence** — append-only file with background rewrite via `fork()`
- **Transactions** — MULTI/EXEC/DISCARD with command queuing
- **Pub/Sub** — SUBSCRIBE/UNSUBSCRIBE/PUBLISH with per-channel delivery
- **Cursor-based iteration** — SCAN for production-safe keyspace traversal
- **Server introspection** — INFO, DBSIZE, FLUSHDB, latency histogram, slow log
- **50K+ ops/sec** — SET 52K, GET 78K, pipelined GET 523K ops/sec

## Tech Stack

- **Language** — C++17
- **Networking** — POSIX sockets + `epoll` event loop
- **Protocol** — RESP2
- **Storage internals** — custom hash table, skiplist, TTL min-heap
- **Persistence** — AOF with background rewrite (`fork()`)
- **Tooling** — Make, Bash scripts, `redis-cli`, `redis-benchmark`

## Language & Platform Support

- **Language support** — C++17 compiler required (GCC ≥ 7 or Clang ≥ 5)
- **Platform support** — Linux only (`epoll`-based; no macOS/Windows support)
- **Client compatibility** — RESP2 clients (`redis-cli`, `redis-benchmark`, common Redis libraries)

## Quick Start

### Build

```bash
cd simple-redis
make
```

Requires GCC or Clang with C++17 support. Produces `build/simple-redis`.

### Run

```bash
./build/simple-redis [port]
```

Default port is 6379. The server binds to `0.0.0.0`.

### Connect

```bash
redis-cli -p 6379
```

```
127.0.0.1:6379> SET hello world
OK
127.0.0.1:6379> GET hello
"world"
127.0.0.1:6379> LPUSH mylist a b c
(integer) 3
127.0.0.1:6379> LRANGE mylist 0 -1
1) "c"
2) "b"
3) "a"
127.0.0.1:6379> ZADD scores 3.14 pi 2.72 e 1.41 sqrt2
(integer) 3
127.0.0.1:6379> ZRANGE scores 0 -1 WITHSCORES
1) "sqrt2"
2) "1.41"
3) "e"
4) "2.72"
5) "pi"
6) "3.14"
127.0.0.1:6379> INFO server
# Server
redis_version:simple-redis-0.7.0
...
```

## Implemented Commands

| Category | Commands |
|----------|----------|
| String | SET, GET, PING |
| Key | DEL, EXISTS, KEYS, EXPIRE, TTL, PEXPIRE, PTTL, DBSIZE, SCAN |
| List | LPUSH, RPUSH, LPOP, RPOP, LLEN, LRANGE |
| Hash | HSET, HGET, HDEL, HGETALL, HLEN |
| Set | SADD, SREM, SISMEMBER, SMEMBERS, SCARD |
| Sorted Set | ZADD, ZREM, ZSCORE, ZRANK, ZRANGE, ZCARD |
| Transaction | MULTI, EXEC, DISCARD |
| Pub/Sub | SUBSCRIBE, UNSUBSCRIBE, PUBLISH |
| Server | INFO, FLUSHDB, BGREWRITEAOF |

## Architecture

```
┌─────────────────────────────────────────────┐
│  main.cpp — orchestrator & event loop       │
├─────────────────────────────────────────────┤
│  cmd/     — command dispatch & handlers     │
├─────────────────────────────────────────────┤
│  proto/   — RESP2 parser & serializer       │
├─────────────────────────────────────────────┤
│  net/     — epoll, listener, connections    │
├─────────────────────────────────────────────┤
│  store/   — hash table, skiplist, TTL heap  │
├──────────── persistence/ overlay ───────────┤
│  AOFWriter, AOFLoader                       │
└─────────────────────────────────────────────┘
```

Four layers with strict dependency rules — each layer may only depend on layers below it. See [docs/architecture.md](docs/architecture.md) for details.

**Key design decisions:**

- **Single-threaded** — no locks, no races, cache-friendly (same model as Redis)
- **Level-triggered epoll** — simple, correct, handles 10K+ connections
- **Incremental rehashing** — hash table grows without blocking the event loop
- **FNV-1a hashing** — fast 64-bit hash with good distribution
- **std::variant** — type-safe polymorphic values without virtual dispatch
- **AOF-only persistence** — RESP-formatted, human-readable, crash-safe with fsync

## Tests

### Unit Tests

```bash
make test
```

Runs 6 unit test suites: buffer, RESP parser, hash table, TTL heap, AOF, skiplist.

### Integration Tests

```bash
# Run all phases
for i in 1 2 3 4 5 6 7; do
    bash tests/integration/test_phase${i}.sh
done
```

7 integration test suites covering all features.

### Stress Test

```bash
bash tests/stress/test_10k_idle_connections.sh
```

Verifies the server handles 10,000 concurrent idle connections.

## Benchmarks

```bash
# Quick benchmark
redis-benchmark -p 6379 -n 100000 -c 50 -q

# With pipelining
redis-benchmark -p 6379 -n 100000 -c 50 -P 20 -t set,get -q

# Full evaluation (starts server, runs all benchmarks)
bash bench/run_full_evaluation.sh
```

See [docs/performance.md](docs/performance.md) for detailed results.

## Documentation

| Document | Description |
|----------|-------------|
| [docs/architecture.md](docs/architecture.md) | Layered architecture, design decisions, data flow |
| [docs/components.md](docs/components.md) | Detailed component descriptions for every class |
| [docs/protocol.md](docs/protocol.md) | RESP2 wire format, parser/serializer design |
| [docs/data_structures.md](docs/data_structures.md) | Hash table, skiplist, TTL heap, buffer internals |
| [docs/persistence.md](docs/persistence.md) | AOF write path, replay, background rewrite |
| [docs/commands.md](docs/commands.md) | Complete command reference with syntax and return values |
| [docs/performance.md](docs/performance.md) | Benchmark results, latency histogram, slow log |

## Project Structure

```
simple-redis/
├── src/
│   ├── main.cpp
│   ├── cmd/          11 files — command dispatch & handlers
│   ├── net/           4 files — epoll, listener, connection, buffer
│   ├── proto/         2 files — RESP2 parser & serializer
│   ├── store/         5 files — database, hash table, skiplist, TTL heap
│   └── persistence/   2 files — AOF writer & loader
├── tests/
│   ├── unit/          6 test files
│   ├── integration/   7 test scripts (one per phase)
│   └── stress/        1 stress test
├── bench/             benchmark scripts & report
├── docs/              technical documentation
└── Makefile
```

## Requirements

- Linux (epoll-based — does not support macOS/Windows)
- GCC ≥ 7 or Clang ≥ 5 with C++17 support
- `redis-cli` for interactive use (optional)
- `redis-benchmark` for performance testing (optional)

## License

This project is for educational purposes — a from-scratch implementation to understand Redis internals.
