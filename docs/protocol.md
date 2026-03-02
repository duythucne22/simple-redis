# RESP2 Protocol

simple-redis implements the RESP (REdis Serialization Protocol) version 2 wire format — the same protocol spoken by `redis-cli` and all standard Redis client libraries.

## Wire Format

RESP2 is a text-based, binary-safe protocol. Every token begins with a type byte and ends with `\r\n`.

### Request Format

Clients send commands as **RESP arrays of bulk strings:**

```
*3\r\n        ← array of 3 elements
$3\r\n        ← bulk string of length 3
SET\r\n
$5\r\n        ← bulk string of length 5
mykey\r\n
$7\r\n        ← bulk string of length 7
myvalue\r\n
```

The server also accepts **inline commands** (a single line of text terminated by `\r\n`, split on spaces). This format is simpler but not binary-safe:

```
SET mykey myvalue\r\n
```

### Response Types

| Prefix | Type | Example | Usage |
|--------|------|---------|-------|
| `+` | Simple String | `+OK\r\n` | SET, PING |
| `-` | Error | `-ERR unknown command\r\n` | Validation failures |
| `:` | Integer | `:42\r\n` | DEL, EXISTS, DBSIZE |
| `$` | Bulk String | `$5\r\nhello\r\n` | GET |
| `$-1` | Null Bulk String | `$-1\r\n` | GET on missing key |
| `*` | Array | `*2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n` | KEYS, LRANGE |

## Parser Design (`RespParser`)

`RespParser` extracts commands from a `Buffer` without intermediate allocations on incomplete frames.

### Parse Algorithm

```
parse(Buffer& buf):
  1. Peek at first byte of readable data.
  2. If '*' → parseArray() [standard RESP].
     Otherwise → parseInline() [inline command].
  3. If incomplete (no full \r\n terminator found) → return nullopt.
     Buffer is NOT modified.
  4. On success → consume parsed bytes from buffer, return vector<string>.
```

### Array Parsing (`parseArray`)

1. Read the line after `*` to get element count N.
2. For each of the N elements:
   - Expect `$` prefix.
   - Read the length line to get L.
   - Read exactly L bytes, then expect `\r\n`.
3. Track total `bytesConsumed` across all elements.
4. On any incomplete data, return `nullopt` — no bytes consumed.

### Inline Parsing (`parseInline`)

1. Scan for the first `\r\n`.
2. If not found, return `nullopt`.
3. Split the line on whitespace. Each token becomes an argument.

### Zero-Copy Incomplete Handling

The parser never modifies the buffer on failure. `findCRLF()` scans the raw byte pointer without copying. Only on a successful parse does the buffer's `consume()` method advance the read cursor. This makes pipelining efficient — partial frames remain in the buffer for the next `poll()` iteration.

## Serializer Design (`RespSerializer`)

`RespSerializer` provides static methods that append RESP tokens to an outgoing `Buffer`. No internal state — every call is independent.

### Method Signatures

```cpp
static void writeSimpleString(Buffer& buf, std::string_view s);
static void writeError(Buffer& buf, std::string_view msg);
static void writeInteger(Buffer& buf, int64_t val);
static void writeBulkString(Buffer& buf, std::string_view s);
static void writeNull(Buffer& buf);
static void writeArrayHeader(Buffer& buf, int64_t count);
```

### Composing Complex Responses

Array responses are built by writing the header first, then individual elements:

```cpp
// Return ["foo", "bar"]
RespSerializer::writeArrayHeader(buf, 2);
RespSerializer::writeBulkString(buf, "foo");
RespSerializer::writeBulkString(buf, "bar");
```

Nested arrays (e.g., EXEC output) follow the same pattern — the caller writes array headers at each level.

## Pipelining

RESP2 supports **command pipelining** — clients send multiple commands without waiting for individual responses. The server processes them sequentially and writes all responses into the outgoing buffer.

In `main.cpp`, the dispatch loop runs in a `while(true)` that keeps calling `parser.parse()` until the buffer is exhausted:

```cpp
while (true) {
    auto cmd = parser.parse(conn.incoming());
    if (!cmd.has_value()) break;  // incomplete frame → wait for more data
    commandTable.dispatch(db, conn, *cmd);
}
```

This naturally handles pipelining — if a client sends 100 commands in one TCP segment, all 100 are parsed and dispatched in a single event loop iteration.

### Pipelining Performance Impact

Pipelining eliminates per-command round-trip latency. Benchmark results show:

| Mode | GET ops/sec |
|------|------------|
| No pipelining | 78,370 |
| Pipeline depth 20 | 523,560 |

A 6.7× throughput improvement from pipelining alone.

## Binary Safety

All data in RESP2 is binary-safe when using bulk strings (`$len\r\n...`). The parser reads exactly `len` bytes regardless of content, so keys and values can contain any byte including `\r`, `\n`, or null bytes. Inline commands are NOT binary-safe because they split on whitespace.

## Compatibility

simple-redis is fully compatible with standard Redis tooling:

- **`redis-cli`** — works out of the box for interactive use.
- **`redis-benchmark`** — works for throughput testing.
- **Client libraries** (redis-py, Jedis, node-redis, etc.) — any RESP2-compatible client can connect.

The server does not implement RESP3 (the Redis 6+ protocol) — all responses use RESP2 format.
