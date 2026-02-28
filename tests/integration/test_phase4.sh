#!/usr/bin/env bash
# tests/integration/test_phase4.sh
#
# Phase 4 integration tests: AOF persistence.
# Tests all 7 acceptance criteria from SIMPLE_REDIS_BUILD_SPEC.md §Phase 4.
#
# Requires: redis-cli (from redis-tools package)
# Usage: bash tests/integration/test_phase4.sh

PORT=6399
SERVER=./build/simple-redis
AOF_FILE=appendonly.aof
PASSED=0
FAILED=0
SERVER_PID=""

# ── Helpers ─────────────────────────────────────────────────────────────────

start_server() {
    # Remove old AOF file for a clean start unless told otherwise.
    if [[ "${KEEP_AOF:-}" != "1" ]]; then
        rm -f "$AOF_FILE"
    fi
    $SERVER "$PORT" &
    SERVER_PID=$!
    sleep 0.8  # give server time to start and load AOF
}

stop_server() {
    if [[ -n "$SERVER_PID" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    SERVER_PID=""
    sleep 0.3
}

restart_server() {
    stop_server
    KEEP_AOF=1 start_server
}

cleanup() {
    stop_server
    rm -f "$AOF_FILE"
    rm -f temp-rewrite-*.aof
}
trap cleanup EXIT

run_test() {
    local name="$1"
    local actual="$2"
    local expected="$3"

    if [[ "$actual" == "$expected" ]]; then
        echo "[PASS] $name"
        PASSED=$((PASSED + 1))
    else
        echo "[FAIL] $name"
        echo "  expected: '$expected'"
        echo "  actual:   '$actual'"
        FAILED=$((FAILED + 1))
    fi
}

cli() {
    redis-cli -p "$PORT" "$@" 2>/dev/null
}

# ── Test 1: SET survives restart ────────────────────────────────────────────
# Acceptance criterion 1: SET foo bar, SET baz qux, kill → restart → GET returns values.
echo "--- Test 1: SET survives restart ---"
start_server

cli SET foo bar > /dev/null
cli SET baz qux > /dev/null
sleep 0.5  # ensure AOF flush

restart_server

run_test "GET foo after restart" "$(cli GET foo)" "bar"
run_test "GET baz after restart" "$(cli GET baz)" "qux"

stop_server

# ── Test 2: DEL survives restart ────────────────────────────────────────────
# Acceptance criterion 2: SET foo bar, DEL foo, restart → GET foo returns (nil).
echo ""
echo "--- Test 2: DEL survives restart ---"
start_server

cli SET foo bar > /dev/null
cli DEL foo > /dev/null
sleep 0.5

restart_server

# redis-cli returns empty string for nil values
RESULT=$(cli GET foo)
run_test "GET foo after DEL + restart" "$RESULT" ""

stop_server

# ── Test 3: EXPIRE survives restart ─────────────────────────────────────────
# Acceptance criterion 3: SET key val; EXPIRE key 3600, restart → TTL key > 0.
echo ""
echo "--- Test 3: EXPIRE survives restart ---"
start_server

cli SET key val > /dev/null
cli EXPIRE key 3600 > /dev/null
sleep 0.5

restart_server

TTL_RESULT=$(cli TTL key)
if [[ "$TTL_RESULT" =~ ^[0-9]+$ ]] && (( TTL_RESULT > 0 )); then
    echo "[PASS] TTL key > 0 after restart (got $TTL_RESULT)"
    PASSED=$((PASSED + 1))
else
    echo "[FAIL] TTL key > 0 after restart (got $TTL_RESULT)"
    FAILED=$((FAILED + 1))
fi

stop_server

# ── Test 4: Benchmark data survives restart ─────────────────────────────────
# Acceptance criterion 4: redis-benchmark -t set -n 10000 → restart → DBSIZE returns 10000.
echo ""
echo "--- Test 4: Benchmark data survives restart ---"
start_server

# -r 10000000: large range to avoid key collisions via birthday paradox.
redis-benchmark -p "$PORT" -t set -n 10000 -r 10000000 -q > /dev/null 2>&1
sleep 1.5  # ensure all AOF writes are flushed

DBSIZE_BEFORE=$(cli DBSIZE)

restart_server

DBSIZE_AFTER=$(cli DBSIZE)
# The core acceptance criterion is that DBSIZE matches before and after restart.
# With random keys, slight collisions are possible so compare before vs after.
if [[ "$DBSIZE_AFTER" == "$DBSIZE_BEFORE" ]]; then
    echo "[PASS] DBSIZE after restart = $DBSIZE_AFTER (matches pre-restart)"
    PASSED=$((PASSED + 1))
else
    echo "[FAIL] DBSIZE after restart = $DBSIZE_AFTER (expected $DBSIZE_BEFORE)"
    FAILED=$((FAILED + 1))
fi

stop_server

# ── Test 5: AOF file grows with each write ──────────────────────────────────
# Acceptance criterion 5: AOF file grows with each write command.
echo ""
echo "--- Test 5: AOF file grows with writes ---"
start_server

SIZE_BEFORE=$(wc -c < "$AOF_FILE" 2>/dev/null || echo 0)
cli SET grow1 val1 > /dev/null
cli SET grow2 val2 > /dev/null
cli SET grow3 val3 > /dev/null
sleep 0.3
SIZE_AFTER=$(wc -c < "$AOF_FILE" 2>/dev/null || echo 0)

if (( SIZE_AFTER > SIZE_BEFORE )); then
    echo "[PASS] AOF file grew from $SIZE_BEFORE to $SIZE_AFTER bytes"
    PASSED=$((PASSED + 1))
else
    echo "[FAIL] AOF file did not grow (before=$SIZE_BEFORE, after=$SIZE_AFTER)"
    FAILED=$((FAILED + 1))
fi

stop_server

# ── Test 6: BGREWRITEAOF compacts the file ──────────────────────────────────
# Acceptance criterion 6: SET key 100 times → BGREWRITEAOF → smaller AOF.
echo ""
echo "--- Test 6: BGREWRITEAOF compacts AOF ---"
start_server

# Write the same key 100 times (each SET creates a new AOF entry).
for i in $(seq 1 100); do
    cli SET rewrite_key "value_$i" > /dev/null
done
sleep 0.5

SIZE_BEFORE_REWRITE=$(wc -c < "$AOF_FILE" 2>/dev/null || echo 0)

# Trigger background rewrite.
cli BGREWRITEAOF > /dev/null
sleep 3  # give rewrite time to complete

SIZE_AFTER_REWRITE=$(wc -c < "$AOF_FILE" 2>/dev/null || echo 0)

if (( SIZE_AFTER_REWRITE < SIZE_BEFORE_REWRITE )); then
    echo "[PASS] AOF compacted from $SIZE_BEFORE_REWRITE to $SIZE_AFTER_REWRITE bytes"
    PASSED=$((PASSED + 1))
else
    echo "[FAIL] AOF not compacted (before=$SIZE_BEFORE_REWRITE, after=$SIZE_AFTER_REWRITE)"
    FAILED=$((FAILED + 1))
fi

# Verify the final value survived compaction.
run_test "Value correct after rewrite" "$(cli GET rewrite_key)" "value_100"

stop_server

# ── Test 7: Corrupted AOF — server loads valid prefix ───────────────────────
# Acceptance criterion 7: Append garbage to AOF → server starts with warning,
# loads valid prefix.
echo ""
echo "--- Test 7: Corrupted AOF recovery ---"
start_server

cli SET valid1 data1 > /dev/null
cli SET valid2 data2 > /dev/null
sleep 0.5

stop_server

# Append garbage to the end of the AOF file.
printf "THIS_IS_GARBAGE_DATA_THAT_IS_NOT_VALID_RESP" >> "$AOF_FILE"

# Restart — should load valid prefix and warn about corruption.
KEEP_AOF=1 start_server

# The first two keys should be present (valid prefix loaded).
run_test "valid1 after corruption" "$(cli GET valid1)" "data1"
run_test "valid2 after corruption" "$(cli GET valid2)" "data2"

stop_server

# ── Summary ──────────────────────────────────────────────────────────────────
echo ""
echo "=================================="
echo "Phase 4 Integration Tests Complete"
echo "Passed: $PASSED"
echo "Failed: $FAILED"
echo "=================================="

if [[ "$FAILED" -gt 0 ]]; then
    exit 1
fi
exit 0
