# Persistence

simple-redis uses Append-Only File (AOF) persistence to survive restarts. Every write command is appended to disk, and on startup the AOF file is replayed to reconstruct the database.

## Why AOF

AOF was chosen over RDB snapshots for several reasons:

- **Simplicity.** Appending RESP-formatted commands to a file is straightforward — no custom binary format, no snapshot serialization.
- **Audit trail.** The AOF file is a human-readable log of every mutation.
- **Natural integration.** The command dispatch pipeline already produces `vector<string>` arguments — logging them is a single function call.
- **Incremental durability.** The `EVERYSEC` fsync policy limits data loss to ~1 second, which is acceptable for most use cases.

## AOF Write Path

### Step 1 — Command Execution

When a client sends a write command (SET, DEL, LPUSH, etc.), the server:

1. Dispatches the command via `CommandTable::dispatch()`.
2. Checks if the command is flagged as a write (via `CommandTable::isWriteCommand()`).
3. If yes, calls `AOFWriter::log(args)`.

```
Client → parse → dispatch → Database mutation → AOFWriter::log()
```

The AOF log happens **after** successful execution, ensuring only valid commands are persisted.

### Step 2 — RESP Serialization to Disk

`AOFWriter::log()` formats the command in standard RESP format and writes it to the file descriptor:

```
*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n
```

A write loop handles partial writes — the full command is guaranteed to be on disk (in the OS page cache) before `log()` returns.

### Step 3 — Fsync

The timing of `fsync()` depends on the configured policy:

| Policy | When fsync runs | Data loss window |
|--------|----------------|-----------------|
| `ALWAYS` | After every `log()` call | At most 1 command |
| `EVERYSEC` | Once per second via `tick()` | Up to ~1 second |
| `NO` | Never (OS decides) | Undefined |

The default is `EVERYSEC`, matching Redis's default. `tick()` is called from the event loop's 100ms timer callback, so the actual fsync interval is between 1.0 and 1.1 seconds.

## AOF Load Path

On startup, `AOFLoader::load()` replays the AOF file:

1. Open the file. If not found, return -1 (normal for a fresh start).
2. Read the entire file into a `Buffer`.
3. Feed the buffer to `RespParser::parse()` in a loop.
4. For each successfully parsed command, call `CommandTable::dispatch()` against the database.
5. Return the count of commands replayed.

### Corruption Handling

If the file is truncated mid-command (e.g., due to a crash during write), the parser will return `nullopt` on the incomplete frame. The loader stops at that point, logs a warning, and returns the count of successfully replayed commands. This "valid prefix" approach matches Redis's `redis-check-aof --fix` behavior.

### Dummy Connection

AOF replay dispatches commands through the normal `CommandTable`, which requires a `Connection&`. The loader creates a dummy `Connection` object with a throwaway fd. Response output written to the dummy's outgoing buffer is silently discarded.

## Background Rewrite

Over time, the AOF file grows indefinitely (e.g., repeated SET/DEL on the same key). Background rewrite compacts it to the minimum set of commands needed to reconstruct the current state.

### Rewrite Flow

```
main thread                      child process
──────────                       ─────────────
1. triggerRewrite()
   fork() ──────────────────────► 2. Iterate database snapshot
                                     Write one command per key
   3. Continue logging to old        to temp file
      file AND rewriteBuffer_    ◄── 4. exit(0)
   5. checkRewriteComplete()
      waitpid(WNOHANG) → child done
   6. Append rewriteBuffer_ to temp file
   7. rename(temp, appendonly.aof)   ← atomic swap
   8. Reopen fd for new file
```

### Key Design Points

- **Fork-based snapshot.** `fork()` creates a copy-on-write snapshot of the database. The child reads from this snapshot while the parent continues serving clients. This is the same technique Redis uses.
- **Rewrite buffer.** Commands that arrive after `fork()` are logged to both the old file and an in-memory `rewriteBuffer_`. After the child finishes, the buffer is appended to the new file before the atomic swap, ensuring no commands are lost.
- **Atomic swap.** `rename()` is atomic on Linux (POSIX guarantee), so the transition from old to new AOF is crash-safe.
- **Non-blocking check.** `checkRewriteComplete()` uses `waitpid(WNOHANG)` — it returns immediately if the child is still running. Called every 100ms from the timer callback.
- **Single rewrite at a time.** If `isRewriting_` is already true, `triggerRewrite()` is a no-op.

### Triggering a Rewrite

Rewrite is triggered manually via the `BGREWRITEAOF` command:

```
redis-cli> BGREWRITEAOF
"Background append only file rewriting started"
```

There is no automatic rewrite trigger (no size-based threshold). This keeps the implementation simple.

## AOF File Format

The file is simply a sequence of RESP commands, one after another:

```
*3\r\n$3\r\nSET\r\n$1\r\na\r\n$1\r\n1\r\n
*3\r\n$3\r\nSET\r\n$1\r\nb\r\n$1\r\n2\r\n
*2\r\n$3\r\nDEL\r\n$1\r\na\r\n
*3\r\n$6\r\nLPUSH\r\n$4\r\nlist\r\n$5\r\nhello\r\n
```

After a background rewrite, the file contains only the minimal commands needed to recreate the current state — one command per key.

## Transaction Interaction

Commands inside a `MULTI`/`EXEC` transaction are logged individually by the `EXEC` handler, not by the main dispatch loop. This ensures:

1. Queued commands are not logged until they actually execute.
2. Each write command in the transaction gets its own AOF entry.
3. If the server crashes mid-transaction (before EXEC), no partial transaction is replayed.

## Configuration

AOF settings are compile-time constants in `main.cpp`:

```cpp
static constexpr const char* kAOFFilename = "appendonly.aof";
static constexpr auto kAOFPolicy = AOFWriter::FsyncPolicy::EVERYSEC;
```

The AOF file is created in the server's working directory.
