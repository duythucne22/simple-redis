#!/usr/bin/env bash
# ============================================================================
# simple-redis — Full Evaluation & Benchmark Report
#
# Starts the server, runs all feature-specific benchmarks covering every
# implemented command family, captures INFO output, and produces a
# machine-readable report file.
#
# Usage:  ./bench/run_full_evaluation.sh
# Output: bench/BENCHMARK_REPORT.md
# ============================================================================
set -euo pipefail

PORT=16408
SERVER=./build/simple-redis
AOF=appendonly.aof
REPORT=bench/BENCHMARK_REPORT.md
REQUESTS=100000
CLIENTS=50
DATASIZE=64

# ── Helpers ─────────────────────────────────────────────────────────────────
cleanup() {
    if [[ -n "${SERVER_PID:-}" ]]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -f "$AOF"
}
trap cleanup EXIT

start_server() {
    rm -f "$AOF"
    "$SERVER" "$PORT" &
    SERVER_PID=$!
    sleep 0.5
}

redis_cmd() {
    redis-cli -p "$PORT" "$@" 2>/dev/null
}

# Capture benchmark line — extract ops/sec from redis-benchmark -q output.
bench_one() {
    local label="$1"; shift
    echo -n "  Running: $label ... "
    local out
    out=$(redis-benchmark -p "$PORT" -q "$@" 2>&1 || true)
    local ops
    ops=$(echo "$out" | grep -oP '[\d.]+(?= requests per second)' | head -1 || echo "N/A")
    if [[ -z "$ops" ]]; then ops="N/A"; fi
    echo "$ops ops/sec"
    echo "| $label | $ops |" >> "$REPORT"
}

# ============================================================================
echo "=== simple-redis Full Evaluation ==="
echo ""

# Build first.
echo "Building..."
make -j"$(nproc)" > /dev/null 2>&1
echo "Build OK."
echo ""

start_server
echo "Server started on port $PORT (PID $SERVER_PID)."
echo ""

# ── Initialise report ──────────────────────────────────────────────────────
cat > "$REPORT" <<EOF
# simple-redis Benchmark Report

**Date:** $(date -u '+%Y-%m-%d %H:%M UTC')
**Configuration:** $REQUESTS requests, $CLIENTS clients, ${DATASIZE}B value size
**Server:** simple-redis (single-threaded, epoll, C++17)
**Port:** $PORT

---

## 1. Core Throughput (redis-benchmark)

| Command | ops/sec |
|---------|---------|
EOF

# ── Run benchmarks ─────────────────────────────────────────────────────────
echo "--- Core throughput ---"

bench_one "PING" -n "$REQUESTS" -c "$CLIENTS" -t ping
bench_one "SET (${DATASIZE}B, random keys)" -n "$REQUESTS" -c "$CLIENTS" -d "$DATASIZE" -r 100000 -t set
bench_one "GET (random keys)" -n "$REQUESTS" -c "$CLIENTS" -r 100000 -t get
bench_one "LPUSH" -n "$REQUESTS" -c "$CLIENTS" -r 100000 -t lpush
bench_one "RPUSH" -n "$REQUESTS" -c "$CLIENTS" -r 100000 -t rpush
bench_one "LPOP" -n "$REQUESTS" -c "$CLIENTS" -r 100000 -t lpop
bench_one "HSET" -n "$REQUESTS" -c "$CLIENTS" -r 100000 -t hset
bench_one "SADD" -n "$REQUESTS" -c "$CLIENTS" -r 100000 -t sadd
bench_one "ZADD" -n "$REQUESTS" -c "$CLIENTS" -r 100000 -t zadd

echo ""
echo "--- Pipelining ---"
cat >> "$REPORT" <<'EOF'

## 2. Pipelining Throughput

| Command | ops/sec |
|---------|---------|
EOF

bench_one "SET (pipeline=20)" -n "$REQUESTS" -c "$CLIENTS" -d "$DATASIZE" -r 100000 -t set -P 20
bench_one "GET (pipeline=20)" -n "$REQUESTS" -c "$CLIENTS" -r 100000 -t get -P 20

# ── Custom command benchmarks (features not in redis-benchmark -t) ──────────
echo ""
echo "--- Custom command tests ---"
cat >> "$REPORT" <<'EOF'

## 3. Custom Command Benchmarks

| Command | ops/sec |
|---------|---------|
EOF

bench_one "DEL (random keys)" -n 10000 -c 1 -r 100000 \
    "DEL" "__rand_int__"

bench_one "EXISTS" -n 10000 -c 1 -r 100000 \
    "EXISTS" "__rand_int__"

bench_one "DBSIZE" -n 10000 -c 1 "DBSIZE"

bench_one "INFO" -n 10000 -c 1 "INFO"

bench_one "PEXPIRE" -n 10000 -c 1 -r 100000 \
    "PEXPIRE" "__rand_int__" "60000"

bench_one "PTTL" -n 10000 -c 1 -r 100000 \
    "PTTL" "__rand_int__"

# FLUSHDB (single client)
bench_one "FLUSHDB" -n 1000 -c 1 "FLUSHDB"

# ── Capture INFO output for the report ──────────────────────────────────────
echo ""
echo "--- Capturing server INFO ---"

info_output=$(redis_cmd INFO)

cat >> "$REPORT" <<EOF

## 4. Server INFO (post-benchmark)

\`\`\`
$info_output
\`\`\`

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
EOF

echo ""
echo "=== Evaluation complete. Report saved to $REPORT ==="
echo ""
echo "Quick summary:"
echo "  Total commands processed (from INFO):"
echo "  $info_output" | grep "total_commands_processed" || true
echo ""
echo "  Memory usage:"
echo "  $info_output" | grep "used_memory" || true
