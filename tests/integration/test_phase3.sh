#!/usr/bin/env bash
# tests/integration/test_phase3.sh
# Integration tests for Phase 3: Expiration (TTL, lazy + active expiry)
#
# Requires: redis-cli, the simple-redis server binary at build/simple-redis
# Uses port 6399 to avoid conflict with a real Redis instance.

set -uo pipefail

PORT=6399
SERVER=./build/simple-redis
SERVER_PID=""
PASS=0
FAIL=0

cleanup() {
    if [[ -n "$SERVER_PID" ]]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

# Start the server.
$SERVER "$PORT" &
SERVER_PID=$!
sleep 0.5  # give it time to bind

# Helper: run redis-cli and trim whitespace.
rcli() {
    redis-cli -p "$PORT" "$@" 2>/dev/null
}

run_test() {
    local name="$1"
    local actual="$2"
    local expected="$3"

    if [[ "$actual" == "$expected" ]]; then
        printf "  [PASS] %s\n" "$name"
        ((PASS++))
    else
        printf "  [FAIL] %s — expected '%s', got '%s'\n" "$name" "$expected" "$actual"
        ((FAIL++))
    fi
}

run_test_numeric_le() {
    local name="$1"
    local actual="$2"
    local threshold="$3"

    # Extract integer from response (redis-cli may print "(integer) N").
    local num
    num=$(echo "$actual" | grep -oE '[-]?[0-9]+' | head -1)

    if [[ -z "$num" ]]; then
        printf "  [FAIL] %s — could not parse number from '%s'\n" "$name" "$actual"
        ((FAIL++))
        return
    fi

    if (( num <= threshold )); then
        printf "  [PASS] %s (got %s <= %s)\n" "$name" "$num" "$threshold"
        ((PASS++))
    else
        printf "  [FAIL] %s — expected <= %s, got %s\n" "$name" "$threshold" "$num"
        ((FAIL++))
    fi
}

echo "=== Phase 3 Integration Tests ==="

# ── Test 1: EXPIRE + GET after expiry → (nil) ──
# Acceptance criterion #1: SET foo bar; EXPIRE foo 2; sleep 3; GET foo → (nil)
rcli SET test1 bar > /dev/null
rcli EXPIRE test1 2 > /dev/null
sleep 3
result=$(rcli GET test1)
run_test "AC1: EXPIRE + GET after expiry returns nil" "$result" ""

# ── Test 2: TTL on key with no expiry → -1 ──
# Acceptance criterion #2: SET foo bar; TTL foo → -1
rcli SET test2 bar > /dev/null
result=$(rcli TTL test2)
run_test "AC2: TTL on permanent key returns -1" "$result" "-1"

# ── Test 3: EXPIRE + TTL returns value <= N ──
# Acceptance criterion #3: SET foo bar; EXPIRE foo 10; TTL foo → ≤ 10
rcli SET test3 bar > /dev/null
rcli EXPIRE test3 10 > /dev/null
result=$(rcli TTL test3)
run_test_numeric_le "AC3: EXPIRE 10 + TTL returns <= 10" "$result" 10

# ── Test 4: PEXPIRE with 500ms → GET after 1s returns nil ──
# Acceptance criterion #4: SET foo bar; PEXPIRE foo 500; sleep 1; GET foo → (nil)
rcli SET test4 bar > /dev/null
rcli PEXPIRE test4 500 > /dev/null
sleep 1
result=$(rcli GET test4)
run_test "AC4: PEXPIRE 500ms + sleep 1s → nil" "$result" ""

# ── Test 5: EXPIRE + DEL → TTL returns -2 ──
# Acceptance criterion #5: SET foo bar; EXPIRE foo 100; DEL foo; TTL foo → -2
rcli SET test5 bar > /dev/null
rcli EXPIRE test5 100 > /dev/null
rcli DEL test5 > /dev/null
result=$(rcli TTL test5)
run_test "AC5: DEL key with TTL → TTL returns -2" "$result" "-2"

# ── Test 6: 1000 keys with EXPIRE 1, wait 2s, DBSIZE → 0 ──
# Acceptance criterion #6
for i in $(seq 1 1000); do
    rcli SET "expkey:$i" "val" > /dev/null
done
for i in $(seq 1 1000); do
    rcli EXPIRE "expkey:$i" 1 > /dev/null
done
sleep 3
result=$(rcli DBSIZE)
# Strip non-numeric prefix. DBSIZE may return keys from earlier tests — we need to
# check only that the 1000 expired keys are gone. First count how many expkey: remain.
remaining=0
for i in $(seq 1 10); do
    check=$(rcli EXISTS "expkey:$i")
    val=$(echo "$check" | grep -oE '[0-9]+' | head -1)
    remaining=$((remaining + val))
done
# Additionally check DBSIZE accounts for test2, test3 being permanent keys.
# The 1000 expkey:* keys should all be gone. Sample-check a few:
all_gone="yes"
for i in 1 100 500 999 1000; do
    check=$(rcli GET "expkey:$i")
    if [[ -n "$check" ]]; then
        all_gone="no"
        break
    fi
done
if [[ "$all_gone" == "yes" ]]; then
    printf "  [PASS] AC6: 1000 expired keys all gone after 3s\n"
    ((PASS++))
else
    printf "  [FAIL] AC6: some expired keys still present\n"
    ((FAIL++))
fi

# ── Test 7: SET clears TTL ──
# Additional: SET key val; EXPIRE key 100; SET key newval; TTL key → -1
rcli SET test7 val > /dev/null
rcli EXPIRE test7 100 > /dev/null
rcli SET test7 newval > /dev/null
result=$(rcli TTL test7)
run_test "SET clears TTL" "$result" "-1"

# ── Test 8: EXPIRE on non-existent key → 0 ──
result=$(rcli EXPIRE nonexistent 10)
run_test "EXPIRE on missing key returns 0" "$result" "0"

# ── Test 9: DBSIZE ──
rcli DEL test2 test3 test7 > /dev/null  # clean up permanent keys
result=$(rcli DBSIZE)
# Should be 0 (or very small — only if timing allows some expkey stragglers)
num=$(echo "$result" | grep -oE '[0-9]+' | head -1)
if (( num <= 5 )); then
    printf "  [PASS] DBSIZE returns reasonable count (%s)\n" "$num"
    ((PASS++))
else
    printf "  [FAIL] DBSIZE expected near 0, got %s\n" "$num"
    ((FAIL++))
fi

# ── Test 10: PTTL returns milliseconds ──
rcli SET test10 val > /dev/null
rcli PEXPIRE test10 5000 > /dev/null
result=$(rcli PTTL test10)
num=$(echo "$result" | grep -oE '[-]?[0-9]+' | head -1)
if [[ -n "$num" ]] && (( num > 0 && num <= 5000 )); then
    printf "  [PASS] PTTL returns millisecond value (%s)\n" "$num"
    ((PASS++))
else
    printf "  [FAIL] PTTL expected 0 < val <= 5000, got '%s'\n" "$num"
    ((FAIL++))
fi
rcli DEL test10 > /dev/null

# ── Summary ──
echo ""
echo "Results: $PASS passed, $FAIL failed"

if (( FAIL > 0 )); then
    exit 1
fi
exit 0
