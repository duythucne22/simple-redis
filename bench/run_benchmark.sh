#!/usr/bin/env bash
# ============================================================================
# simple-redis — Quick Benchmark (redis-benchmark)
#
# Usage:  ./bench/run_benchmark.sh [port]
#         Default port: 6379 (assumes server is already running)
# ============================================================================
set -euo pipefail

PORT=${1:-6379}
REQUESTS=100000
CLIENTS=50
DATASIZE=64

echo "============================================"
echo " simple-redis benchmark  (port $PORT)"
echo " requests=$REQUESTS  clients=$CLIENTS  data=${DATASIZE}B"
echo "============================================"
echo ""

# ── Core string operations ──────────────────────────────────────────────────
echo ">>> SET (random keys, ${DATASIZE}B value)"
redis-benchmark -p "$PORT" -n "$REQUESTS" -c "$CLIENTS" -d "$DATASIZE" \
    -r 100000 -t set -q

echo ">>> GET (random keys)"
redis-benchmark -p "$PORT" -n "$REQUESTS" -c "$CLIENTS" \
    -r 100000 -t get -q

# ── List operations ─────────────────────────────────────────────────────────
echo ">>> LPUSH"
redis-benchmark -p "$PORT" -n "$REQUESTS" -c "$CLIENTS" \
    -r 100000 -t lpush -q

echo ">>> LPOP"
redis-benchmark -p "$PORT" -n "$REQUESTS" -c "$CLIENTS" \
    -r 100000 -t lpop -q

echo ">>> RPUSH"
redis-benchmark -p "$PORT" -n "$REQUESTS" -c "$CLIENTS" \
    -r 100000 -t rpush -q

# ── Hash operations ─────────────────────────────────────────────────────────
echo ">>> HSET"
redis-benchmark -p "$PORT" -n "$REQUESTS" -c "$CLIENTS" \
    -r 100000 -t hset -q

# ── Set operations ──────────────────────────────────────────────────────────
echo ">>> SADD"
redis-benchmark -p "$PORT" -n "$REQUESTS" -c "$CLIENTS" \
    -r 100000 -t sadd -q

# ── Sorted set operations ──────────────────────────────────────────────────
echo ">>> ZADD"
redis-benchmark -p "$PORT" -n "$REQUESTS" -c "$CLIENTS" \
    -r 100000 -t zadd -q

# ── Pipelining comparison ──────────────────────────────────────────────────
echo ""
echo ">>> SET with pipelining (batch 20)"
redis-benchmark -p "$PORT" -n "$REQUESTS" -c "$CLIENTS" -d "$DATASIZE" \
    -r 100000 -t set -P 20 -q

echo ">>> GET with pipelining (batch 20)"
redis-benchmark -p "$PORT" -n "$REQUESTS" -c "$CLIENTS" \
    -r 100000 -t get -P 20 -q

# ── PING (baseline) ────────────────────────────────────────────────────────
echo ""
echo ">>> PING (baseline)"
redis-benchmark -p "$PORT" -n "$REQUESTS" -c "$CLIENTS" \
    -t ping -q

echo ""
echo "============================================"
echo " Benchmark complete."
echo "============================================"
