# Architecture

## Overview

simple-redis is a single-threaded, event-driven Redis-compatible server written in C++17 for Linux. It processes all commands on one thread using non-blocking I/O via `epoll`, following the same concurrency model as Redis itself. This design eliminates locking, avoids context-switch overhead, and keeps the implementation straightforward while still achieving high throughput.

## Layered Architecture

The codebase is organized into four layers plus a persistence overlay. Each layer has strict dependency rules — layers may only depend on layers below them, and no layer may reach upward.

```
┌───────────────────────────────────────────────────┐
│                    main.cpp                       │  Orchestrator
│         (wiring, event loop, signal handling)     │
├───────────────────────────────────────────────────┤
│  Layer 3: cmd/         Command dispatch & logic   │
│  (CommandTable, StringCommands, KeyCommands, ...) │
├───────────────────────────────────────────────────┤
│  Layer 2: proto/       RESP2 protocol codec       │
│  (RespParser, RespSerializer)                     │
├───────────────────────────────────────────────────┤
│  Layer 1: net/         Network I/O primitives     │
│  (EventLoop, Listener, Connection, Buffer)        │
├───────────────────────────────────────────────────┤
│  Layer 0: store/       In-memory data structures  │
│  (Database, HashTable, RedisObject, TTLHeap, ...) │
├─────────────────────── overlay────────────────────┤
│  persistence/          AOF writer & loader        │
│  (AOFWriter, AOFLoader)                           │
└───────────────────────────────────────────────────┘
```

### Layer 0 — Store (`src/store/`)

The bottom layer owns all in-memory state. It provides a `Database` facade over a `HashTable` (the primary key-value store), a `TTLHeap` (min-heap for active expiry), and `RedisObject` (the polymorphic value type). A `Skiplist` provides ordered indexing for sorted sets.

**Dependency rule:** Must not know about networking, RESP serialization, or command names. Only standard C++ and POSIX types.

### Layer 1 — Network (`src/net/`)

Manages raw TCP connectivity. `Listener` binds a non-blocking socket and accepts clients. `EventLoop` wraps the `epoll` instance and fires a periodic timer callback. `Connection` owns per-client read/write `Buffer` objects and provides `handleRead()` / `handleWrite()` for I/O. `Buffer` implements a zero-copy, two-cursor byte buffer with three-tier compaction.

**Dependency rule:** Must not know about RESP, commands, or the database.

### Layer 2 — Protocol (`src/proto/`)

Encodes and decodes the RESP2 wire format. `RespParser` extracts commands from a `Buffer` without copying bytes on incomplete frames. `RespSerializer` writes response tokens (`+OK\r\n`, `$len\r\n...`, etc.) into an outgoing `Buffer`.

**Dependency rule:** May reference `Buffer` (Layer 1). Must not know about commands, the database, or specific socket fds.

### Layer 3 — Commands (`src/cmd/`)

Contains all command implementations, organized by data type: `StringCommands`, `KeyCommands`, `ListCommands`, `HashCommands`, `SetCommands`, `ZSetCommands`, `TransactionCommands`, `PubSubCommands`, and `ServerCommands`. Each module registers its handlers with `CommandTable`, which provides O(1) dispatch by command name and validates arity before calling the handler.

**Dependency rule:** May use `Database` (Layer 0), `Connection` / `Buffer` (Layer 1), and `RespSerializer` (Layer 2). Must not access epoll or the listener directly.

### Persistence Overlay (`src/persistence/`)

`AOFWriter` appends write commands to disk in RESP format. It supports three fsync policies (ALWAYS, EVERYSEC, NO) and background rewrite via `fork()`. `AOFLoader` replays the AOF file on startup by parsing RESP commands and dispatching them through `CommandTable`.

**Dependency rule:** May use `Database` and `Buffer`/`RespParser` for replay. Must not include anything from `net/` for socket operations.

### Orchestrator (`src/main.cpp`)

The only file that sees all layers. It creates the `Listener`, `EventLoop`, `Database`, `CommandTable`, `AOFWriter`, and `PubSubRegistry`, then enters the main event loop. It also handles signal setup, fd limit raising, connection lifecycle, transaction queuing, pub/sub gating, and timed command dispatch for metrics.

## Design Decisions (ADRs)

### ADR-001: Single-Threaded Execution

All client commands execute on a single thread. This eliminates data races, avoids mutex contention, and simplifies reasoning about state. The only exception is AOF background rewrite, which forks a child process to write a snapshot — the child never modifies shared memory.

**Trade-off:** CPU-bound workloads cannot scale across cores. In practice, Redis itself uses the same model and handles >100K ops/sec per core.

### ADR-002: Type/Encoding Separation

Each `RedisObject` carries both a `DataType` tag (STRING, LIST, HASH, SET, ZSET) and an `Encoding` tag (RAW, INTEGER, LINKEDLIST, HASHTABLE, SKIPLIST). This mirrors Redis's object system where the same logical type can have different internal representations — for example, a STRING might be stored as an `int64_t` if its value is a valid integer.

### ADR-003: `std::variant` for Value Storage

`RedisObject` stores its payload in a `std::variant<std::string, int64_t, std::deque<std::string>, std::unordered_map<...>, std::unordered_set<...>, ZSetData>`. This avoids heap allocation for the variant itself and provides type-safe access via `std::get<>`. The trade-off is that `RedisObject` is move-only (because `Skiplist` is non-copyable).

### ADR-004: Layered Architecture

Strict layer boundaries prevent circular dependencies and make each component independently testable. For example, the `HashTable` unit test exercises rehashing without any networking or RESP code.

### ADR-005: AOF-Only Persistence

The server uses Append-Only File persistence rather than RDB snapshots. AOF is simpler to implement correctly, provides a clear audit trail, and integrates naturally with the command dispatch pipeline (every write command is logged after execution). Background rewrite via `fork()` keeps the AOF file compact without blocking the main thread.

## Data Flow

A typical SET command follows this path:

```
Client TCP data
  → epoll_wait (EventLoop)
  → read() into Connection.incoming (Buffer)
  → RespParser.parse(Buffer) → ["SET", "key", "value"]
  → CommandTable.dispatch()
    → StringCommands::cmdSet(Database, Connection, args)
      → Database::set(key, value)
        → HashTable::set(key, RedisObject::createString(value))
      → RespSerializer::writeSimpleString(Connection.outgoing, "OK")
  → AOFWriter.log(["SET", "key", "value"])   // persistence
  → ServerMetrics.recordLatency(durationUs)   // instrumentation
  → epoll_wait detects EPOLLOUT
  → write() from Connection.outgoing
```

## Concurrency Model

The server uses **level-triggered epoll** in a single-threaded loop:

1. `epoll_wait()` returns ready file descriptors.
2. The listener fd is handled first — all pending `accept()` calls are drained.
3. Client fds are processed: read → parse → dispatch → queue write.
4. After processing events, incremental rehashing runs once per tick.
5. A sweep pass enables `EPOLLOUT` for connections with pending output (needed for cross-connection writes like PUBLISH).
6. Closed connections are cleaned up.
7. Every 100ms, a timer callback runs active expiry, AOF fsync, and rewrite-child checks.

## Directory Structure

```
src/
├── main.cpp              Entry point and orchestrator
├── cmd/                  Command implementations (Layer 3)
│   ├── CommandTable.h/.cpp
│   ├── StringCommands.h/.cpp
│   ├── KeyCommands.h/.cpp
│   ├── ListCommands.h/.cpp
│   ├── HashCommands.h/.cpp
│   ├── SetCommands.h/.cpp
│   ├── ZSetCommands.h/.cpp
│   ├── TransactionCommands.h/.cpp
│   ├── PubSubCommands.h/.cpp
│   ├── PubSubRegistry.h/.cpp
│   └── ServerCommands.h/.cpp
├── net/                  Network primitives (Layer 1)
│   ├── Buffer.h/.cpp
│   ├── Connection.h/.cpp
│   ├── EventLoop.h/.cpp
│   └── Listener.h/.cpp
├── proto/                RESP2 codec (Layer 2)
│   ├── RespParser.h/.cpp
│   └── RespSerializer.h/.cpp
├── store/                Data structures (Layer 0)
│   ├── Database.h/.cpp
│   ├── HashTable.h/.cpp
│   ├── RedisObject.h/.cpp
│   ├── Skiplist.h/.cpp
│   └── TTLHeap.h/.cpp
└── persistence/          AOF overlay
    ├── AOFWriter.h/.cpp
    └── AOFLoader.h/.cpp
```
