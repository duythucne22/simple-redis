# simple-redis Benchmark Report

**Date:** 2026-03-02 16:28 UTC
**Configuration:** 100000 requests, 50 clients, 64B value size
**Server:** simple-redis (single-threaded, epoll, C++17)
**Port:** 16408

---

## 1. Core Throughput (redis-benchmark)

| Command | ops/sec |
|---------|---------|
| PING | 55803.57 |
| SET (64B, random keys) | 52002.08 |
| GET (random keys) | 78369.91 |
| LPUSH | 20056.16 |
| RPUSH | 34025.18 |
| LPOP | 35026.27 |
| HSET | 39872.41 |
| SADD | 41084.63 |
| ZADD | 36576.45 |

## 2. Pipelining Throughput

| Command | ops/sec |
|---------|---------|
| SET (pipeline=20) | 140845.08 |
| GET (pipeline=20) | 523560.22 |

## 3. Custom Command Benchmarks

| Command | ops/sec |
|---------|---------|
| DEL (random keys) | 14992.50 |
| EXISTS | 19157.09 |
| DBSIZE | 24330.90 |
| INFO | 229.48 |
| PEXPIRE | 20202.02 |
| PTTL | 27397.26 |
| FLUSHDB | 3787.88 |

## 4. Server INFO (post-benchmark)

```
# Server
redis_version:simple-redis-0.7.0
process_id:54928
tcp_port:16408
uptime_in_seconds:72
uptime_in_days:0

# Clients
connected_clients:1

# Memory
used_memory:0

# Stats
total_commands_processed:1261036
latency_histogram_us_lt100:1250943
latency_histogram_us_lt500:69
latency_histogram_us_lt1000:6
latency_histogram_us_lt10000:10015
latency_histogram_us_lt100000:2
latency_histogram_us_gte100000:1
slowlog_len:3

# Keyspace

```

## 5. Feature Coverage Summary

| Feature | Phase | Tested |
|---------|-------|--------|
| String (SET/GET/PING) | 2 | Yes |
| Key management (DEL/EXISTS/KEYS/SCAN/RENAME/TYPE) | 2 | Yes |
| TTL / expiry (PEXPIRE/PTTL) | 3 | Yes |
| AOF persistence (BGREWRITEAOF) | 4 | Yes |
| List (LPUSH/RPUSH/LPOP/RPOP/LRANGE/LLEN) | 5 | Yes |
| Hash (HSET/HGET/HDEL/HGETALL/HLEN) | 5 | Yes |
| Set (SADD/SREM/SISMEMBER/SMEMBERS/SCARD) | 5 | Yes |
| Sorted set (ZADD/ZREM/ZSCORE/ZRANK/ZRANGE/ZCOUNT) | 5 | Yes |
| Transactions (MULTI/EXEC/DISCARD) | 6 | Yes |
| Pub/Sub (SUBSCRIBE/UNSUBSCRIBE/PUBLISH) | 6 | Via integration tests |
| Server introspection (INFO/DBSIZE/FLUSHDB) | 7 | Yes |
| Memory tracking | 7 | Yes |
| Latency histogram | 7 | Yes |
| Slow log | 7 | Yes |

## 6. Notes

- All benchmarks run single-threaded on the server side (ADR-001).
- Pipelining significantly improves throughput by reducing round-trip overhead.
- Memory tracking is per-object estimation (sizeof + dynamic data).
- Slow log threshold default: 10,000 µs (10 ms). Configurable at startup.
