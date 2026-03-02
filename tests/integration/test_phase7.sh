#!/usr/bin/env bash
# ============================================================================
# Phase 7 Integration Tests — INFO, DBSIZE, FLUSHDB, Memory, Slow Log
# ============================================================================
set -euo pipefail

PORT=16407
SERVER=./build/simple-redis
AOF=appendonly.aof
PASS=0
FAIL=0

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
    sleep 0.3  # give the server time to bind
}

redis_cmd() {
    redis-cli -p "$PORT" "$@" 2>/dev/null
}

assert_eq() {
    local desc="$1" expected="$2" actual="$3"
    if [[ "$actual" == "$expected" ]]; then
        echo "  PASS: $desc"
        ((PASS++)) || true
    else
        echo "  FAIL: $desc"
        echo "    expected: '$expected'"
        echo "    actual:   '$actual'"
        ((FAIL++)) || true
    fi
}

assert_contains() {
    local desc="$1" needle="$2" haystack="$3"
    if [[ "$haystack" == *"$needle"* ]]; then
        echo "  PASS: $desc"
        ((PASS++)) || true
    else
        echo "  FAIL: $desc"
        echo "    expected to contain: '$needle'"
        echo "    actual: '$haystack'"
        ((FAIL++)) || true
    fi
}

assert_gt() {
    local desc="$1" val="$2" threshold="$3"
    if (( val > threshold )); then
        echo "  PASS: $desc ($val > $threshold)"
        ((PASS++)) || true
    else
        echo "  FAIL: $desc ($val <= $threshold)"
        ((FAIL++)) || true
    fi
}

# ============================================================================
echo "=== Phase 7 Integration Tests ==="
# ============================================================================

start_server

# ── Test 1: INFO returns all 5 sections ────────────────────────────────────
echo ""
echo "--- Test 1: INFO (all sections) ---"

info_all=$(redis_cmd INFO)

assert_contains "INFO has Server section"   "# Server"   "$info_all"
assert_contains "INFO has Clients section"  "# Clients"  "$info_all"
assert_contains "INFO has Memory section"   "# Memory"   "$info_all"
assert_contains "INFO has Stats section"    "# Stats"    "$info_all"
assert_contains "INFO has Keyspace section" "# Keyspace" "$info_all"

assert_contains "INFO has uptime_in_seconds" "uptime_in_seconds:" "$info_all"
assert_contains "INFO has connected_clients" "connected_clients:" "$info_all"
assert_contains "INFO has used_memory"       "used_memory:"       "$info_all"
assert_contains "INFO has total_commands"    "total_commands_processed:" "$info_all"

# ── Test 2: INFO server returns ONLY server section ────────────────────────
echo ""
echo "--- Test 2: INFO server (single section) ---"

info_server=$(redis_cmd INFO server)

assert_contains "INFO server has Server section" "# Server"  "$info_server"

# Should NOT contain other sections.
if [[ "$info_server" != *"# Clients"* ]]; then
    echo "  PASS: INFO server does NOT contain Clients section"
    ((PASS++)) || true
else
    echo "  FAIL: INFO server should NOT contain Clients section"
    ((FAIL++)) || true
fi

if [[ "$info_server" != *"# Memory"* ]]; then
    echo "  PASS: INFO server does NOT contain Memory section"
    ((PASS++)) || true
else
    echo "  FAIL: INFO server should NOT contain Memory section"
    ((FAIL++)) || true
fi

# ── Test 3: DBSIZE on empty database ──────────────────────────────────────
echo ""
echo "--- Test 3: DBSIZE ---"

dbsize_empty=$(redis_cmd DBSIZE)
# redis-cli may format as "(integer) 0" — extract the number.
dbsize_num=$(echo "$dbsize_empty" | grep -oP '\d+' | tail -1)
assert_eq "DBSIZE on empty db" "0" "$dbsize_num"

# Add some keys.
redis_cmd SET key1 value1 > /dev/null
redis_cmd SET key2 value2 > /dev/null
redis_cmd SET key3 value3 > /dev/null

dbsize_3=$(redis_cmd DBSIZE)
dbsize_num=$(echo "$dbsize_3" | grep -oP '\d+' | tail -1)
assert_eq "DBSIZE after 3 SETs" "3" "$dbsize_num"

# ── Test 4: FLUSHDB ───────────────────────────────────────────────────────
echo ""
echo "--- Test 4: FLUSHDB ---"

flush_result=$(redis_cmd FLUSHDB)
assert_eq "FLUSHDB returns OK" "OK" "$flush_result"

dbsize_after=$(redis_cmd DBSIZE)
dbsize_num=$(echo "$dbsize_after" | grep -oP '\d+' | tail -1)
assert_eq "DBSIZE after FLUSHDB" "0" "$dbsize_num"

# ── Test 5: Memory tracking — increases on SET, decreases on DEL ──────────
echo ""
echo "--- Test 5: Memory tracking ---"

# Start clean.
redis_cmd FLUSHDB > /dev/null

info_mem0=$(redis_cmd INFO memory)
mem0=$(echo "$info_mem0" | grep -oP 'used_memory:\K\d+')

# Set several keys.
for i in $(seq 1 100); do
    redis_cmd SET "memkey$i" "$(printf 'x%.0s' $(seq 1 100))" > /dev/null
done

info_mem1=$(redis_cmd INFO memory)
mem1=$(echo "$info_mem1" | grep -oP 'used_memory:\K\d+')

assert_gt "Memory increased after 100 SETs" "$mem1" "$mem0"

# Delete half.
for i in $(seq 1 50); do
    redis_cmd DEL "memkey$i" > /dev/null
done

info_mem2=$(redis_cmd INFO memory)
mem2=$(echo "$info_mem2" | grep -oP 'used_memory:\K\d+')

if (( mem2 < mem1 )); then
    echo "  PASS: Memory decreased after DEL ($mem2 < $mem1)"
    ((PASS++)) || true
else
    echo "  FAIL: Memory should decrease after DEL ($mem2 >= $mem1)"
    ((FAIL++)) || true
fi

# ── Test 6: Stats section shows command count ─────────────────────────────
echo ""
echo "--- Test 6: Stats / total_commands_processed ---"

info_stats=$(redis_cmd INFO stats)
cmd_count=$(echo "$info_stats" | grep -oP 'total_commands_processed:\K\d+')

assert_gt "total_commands_processed > 0" "$cmd_count" "0"

# ── Test 7: Latency histogram presence ────────────────────────────────────
echo ""
echo "--- Test 7: Latency histogram ---"

assert_contains "Histogram has lt100 bucket"    "latency_histogram_us_lt100:"    "$info_stats"
assert_contains "Histogram has lt500 bucket"    "latency_histogram_us_lt500:"    "$info_stats"
assert_contains "Histogram has lt1000 bucket"   "latency_histogram_us_lt1000:"   "$info_stats"
assert_contains "Histogram has lt10000 bucket"  "latency_histogram_us_lt10000:"  "$info_stats"
assert_contains "Histogram has lt100000 bucket" "latency_histogram_us_lt100000:" "$info_stats"
assert_contains "Histogram has gte100000 bucket" "latency_histogram_us_gte100000:" "$info_stats"

# ── Test 8: Slow log presence ─────────────────────────────────────────────
echo ""
echo "--- Test 8: Slow log ---"

assert_contains "Stats has slowlog_len" "slowlog_len:" "$info_stats"

# ── Test 9: INFO keyspace after adding keys ───────────────────────────────
echo ""
echo "--- Test 9: INFO keyspace ---"

# Keys remain from Test 5 (50 of them still exist).
info_ks=$(redis_cmd INFO keyspace)
assert_contains "Keyspace has db0" "db0:keys=" "$info_ks"

# ── Test 10: FLUSHDB resets memory to 0 ──────────────────────────────────
echo ""
echo "--- Test 10: FLUSHDB resets memory ---"

redis_cmd FLUSHDB > /dev/null
info_mem_flush=$(redis_cmd INFO memory)
mem_flush=$(echo "$info_mem_flush" | grep -oP 'used_memory:\K\d+')
assert_eq "Memory is 0 after FLUSHDB" "0" "$mem_flush"

# ============================================================================
echo ""
echo "=== Phase 7 Results: $PASS passed, $FAIL failed ==="
if [[ "$FAIL" -gt 0 ]]; then
    exit 1
fi
