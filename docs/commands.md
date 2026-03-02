# Commands

simple-redis implements 35 commands across 8 categories. All commands are case-insensitive. This reference documents each command's syntax, behavior, and return value.

---

## String Commands

### PING

```
PING [message]
```

Returns `PONG` if called without arguments, or echoes the given message as a bulk string.

**Return:** Simple string `PONG`, or bulk string `message`.

---

### SET

```
SET key value
```

Set a key to a string value. Overwrites any existing value and removes any TTL.

**Return:** Simple string `OK`.

---

### GET

```
GET key
```

Get the value of a key. Returns an error if the key holds a non-string value.

**Return:** Bulk string (the value), or null bulk string if the key does not exist.

---

## Key Commands

### DEL

```
DEL key [key ...]
```

Delete one or more keys.

**Return:** Integer — the number of keys that were deleted.

---

### EXISTS

```
EXISTS key [key ...]
```

Check if one or more keys exist.

**Return:** Integer — the number of specified keys that exist.

---

### KEYS

```
KEYS pattern
```

Return all keys matching the given glob-style pattern. Use `*` to match all keys.

> **Warning:** KEYS scans the entire keyspace. Avoid in production — use SCAN instead.

**Return:** Array of bulk strings (matching key names).

---

### EXPIRE

```
EXPIRE key seconds
```

Set a timeout on a key, in seconds. After the timeout, the key is automatically deleted.

**Return:** Integer — `1` if the timeout was set, `0` if the key does not exist.

---

### TTL

```
TTL key
```

Return the remaining time to live of a key, in seconds.

**Return:** Integer — TTL in seconds, `-1` if the key has no TTL, `-2` if the key does not exist.

---

### PEXPIRE

```
PEXPIRE key milliseconds
```

Set a timeout on a key, in milliseconds.

**Return:** Integer — `1` if the timeout was set, `0` if the key does not exist.

---

### PTTL

```
PTTL key
```

Return the remaining time to live of a key, in milliseconds.

**Return:** Integer — TTL in milliseconds, `-1` if the key has no TTL, `-2` if the key does not exist.

---

### DBSIZE

```
DBSIZE
```

Return the number of keys in the database.

**Return:** Integer — the key count.

> Note: DBSIZE is registered in both `KeyCommands` and `ServerCommands`. The `ServerCommands` registration takes precedence.

---

### SCAN

```
SCAN cursor [COUNT count] [MATCH pattern]
```

Incrementally iterate the keyspace. Returns a cursor and a batch of keys. Pass the returned cursor in the next call to continue iteration. A cursor of `0` starts a new iteration; a returned cursor of `0` means iteration is complete.

**Return:** Array of two elements: `[nextCursor, [key1, key2, ...]]`.

---

## List Commands

### LPUSH

```
LPUSH key element [element ...]
```

Insert one or more elements at the head of a list. Creates the list if it doesn't exist.

**Return:** Integer — the length of the list after the push.

---

### RPUSH

```
RPUSH key element [element ...]
```

Insert one or more elements at the tail of a list. Creates the list if it doesn't exist.

**Return:** Integer — the length of the list after the push.

---

### LPOP

```
LPOP key
```

Remove and return the first element of a list.

**Return:** Bulk string (the popped element), or null if the list is empty or doesn't exist.

---

### RPOP

```
RPOP key
```

Remove and return the last element of a list.

**Return:** Bulk string (the popped element), or null if the list is empty or doesn't exist.

---

### LLEN

```
LLEN key
```

Return the length of a list.

**Return:** Integer — the list length, or `0` if the key doesn't exist.

---

### LRANGE

```
LRANGE key start stop
```

Return a range of elements from a list (0-based, inclusive). Negative indices count from the end (`-1` = last).

**Return:** Array of bulk strings.

---

## Hash Commands

### HSET

```
HSET key field value [field value ...]
```

Set one or more field-value pairs in a hash. Creates the hash if it doesn't exist.

**Return:** Integer — the number of fields that were added (not updated).

---

### HGET

```
HGET key field
```

Get the value of a field in a hash.

**Return:** Bulk string (the value), or null if the field or key doesn't exist.

---

### HDEL

```
HDEL key field [field ...]
```

Delete one or more fields from a hash.

**Return:** Integer — the number of fields that were removed.

---

### HGETALL

```
HGETALL key
```

Return all fields and values in a hash as a flat array: `[field1, value1, field2, value2, ...]`.

**Return:** Array of bulk strings.

---

### HLEN

```
HLEN key
```

Return the number of fields in a hash.

**Return:** Integer — the field count.

---

## Set Commands

### SADD

```
SADD key member [member ...]
```

Add one or more members to a set. Creates the set if it doesn't exist.

**Return:** Integer — the number of members that were added (not already present).

---

### SREM

```
SREM key member [member ...]
```

Remove one or more members from a set.

**Return:** Integer — the number of members that were removed.

---

### SISMEMBER

```
SISMEMBER key member
```

Check if a member exists in a set.

**Return:** Integer — `1` if the member is in the set, `0` otherwise.

---

### SMEMBERS

```
SMEMBERS key
```

Return all members of a set.

**Return:** Array of bulk strings.

---

### SCARD

```
SCARD key
```

Return the number of members in a set.

**Return:** Integer — the set cardinality.

---

## Sorted Set Commands

### ZADD

```
ZADD key score member [score member ...]
```

Add one or more members to a sorted set with scores. If a member already exists, its score is updated.

**Return:** Integer — the number of new members added (not including score updates).

---

### ZSCORE

```
ZSCORE key member
```

Return the score of a member in a sorted set.

**Return:** Bulk string (the score as a string), or null if the member or key doesn't exist.

---

### ZRANK

```
ZRANK key member
```

Return the rank (0-based position) of a member in a sorted set, ordered by score ascending.

**Return:** Integer (the rank), or null if the member doesn't exist.

---

### ZRANGE

```
ZRANGE key start stop [WITHSCORES]
```

Return members in a sorted set between rank `start` and `stop` (inclusive, 0-based). Negative indices count from the end. With `WITHSCORES`, returns `[member1, score1, member2, score2, ...]`.

**Return:** Array of bulk strings.

---

### ZCARD

```
ZCARD key
```

Return the number of members in a sorted set.

**Return:** Integer — the set cardinality.

---

### ZREM

```
ZREM key member [member ...]
```

Remove one or more members from a sorted set.

**Return:** Integer — the number of members removed.

---

## Transaction Commands

### MULTI

```
MULTI
```

Start a transaction. Subsequent commands are queued instead of executed immediately.

**Return:** Simple string `OK`.

---

### EXEC

```
EXEC
```

Execute all queued commands and return their results as an array. Clears the transaction state.

**Return:** Array — one element per queued command's result.

---

### DISCARD

```
DISCARD
```

Discard all queued commands and exit transaction mode.

**Return:** Simple string `OK`.

---

## Pub/Sub Commands

### SUBSCRIBE

```
SUBSCRIBE channel [channel ...]
```

Subscribe to one or more channels. The connection enters subscriber mode — only `SUBSCRIBE`, `UNSUBSCRIBE`, `PING`, and `QUIT` are allowed.

**Return:** For each channel: array `["subscribe", channelName, numSubscriptions]`.

---

### UNSUBSCRIBE

```
UNSUBSCRIBE [channel ...]
```

Unsubscribe from one or more channels, or all channels if none specified.

**Return:** For each channel: array `["unsubscribe", channelName, remainingSubscriptions]`.

---

### PUBLISH

```
PUBLISH channel message
```

Publish a message to a channel. Delivers to all current subscribers.

**Return:** Integer — the number of subscribers that received the message.

---

## Server Commands

### INFO

```
INFO [section]
```

Return server information and statistics. Sections: `server`, `clients`, `memory`, `stats`, `keyspace`, or omit for all.

**Return:** Bulk string — multi-line key-value pairs grouped by section.

**Example output:**

```
# Server
redis_version:simple-redis-0.7.0
process_id:12345
tcp_port:6379
uptime_in_seconds:3600

# Clients
connected_clients:5

# Memory
used_memory:1048576

# Stats
total_commands_processed:50000
latency_histogram_us_lt100:49900
latency_histogram_us_lt500:50
latency_histogram_us_lt1000:30
latency_histogram_us_lt10000:15
latency_histogram_us_lt100000:4
latency_histogram_us_gte100000:1
slowlog_len:2

# Keyspace
db0:keys=1000,expires=50
```

---

### FLUSHDB

```
FLUSHDB [ASYNC]
```

Delete all keys in the database. Resets memory tracking. The `ASYNC` flag is accepted but currently executes synchronously.

**Return:** Simple string `OK`.

---

### BGREWRITEAOF

```
BGREWRITEAOF
```

Trigger a background AOF rewrite. Forks a child process to compact the AOF file.

**Return:** Simple string `Background append only file rewriting started`.

---

## Arity Reference

Arity defines argument count validation:
- **Positive:** Exact argument count required (including the command name).
- **Negative:** Minimum argument count (absolute value). Example: `-3` means at least 3 arguments.

| Command | Arity | Write |
|---------|-------|-------|
| PING | -1 | No |
| SET | 3 | Yes |
| GET | 2 | No |
| DEL | -2 | Yes |
| EXISTS | -2 | No |
| KEYS | 2 | No |
| EXPIRE | 3 | Yes |
| TTL | 2 | No |
| PEXPIRE | 3 | Yes |
| PTTL | 2 | No |
| DBSIZE | 1 | No |
| SCAN | -2 | No |
| LPUSH | -3 | Yes |
| RPUSH | -3 | Yes |
| LPOP | 2 | Yes |
| RPOP | 2 | Yes |
| LLEN | 2 | No |
| LRANGE | 4 | No |
| HSET | -4 | Yes |
| HGET | 3 | No |
| HDEL | -3 | Yes |
| HGETALL | 2 | No |
| HLEN | 2 | No |
| SADD | -3 | Yes |
| SREM | -3 | Yes |
| SISMEMBER | 3 | No |
| SMEMBERS | 2 | No |
| SCARD | 2 | No |
| ZADD | -4 | Yes |
| ZSCORE | 3 | No |
| ZRANK | 3 | No |
| ZRANGE | -4 | No |
| ZCARD | 2 | No |
| ZREM | -3 | Yes |
| MULTI | 1 | No |
| DISCARD | 1 | No |
| EXEC | 1 | No |
| SUBSCRIBE | -2 | No |
| UNSUBSCRIBE | -1 | No |
| PUBLISH | 3 | No |
| INFO | -1 | No |
| FLUSHDB | -1 | Yes |
| BGREWRITEAOF | 1 | No |
