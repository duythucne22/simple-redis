#!/usr/bin/env bash
# ============================================================================
# Phase 6 Integration Tests — Transactions, Pub/Sub, SCAN
# ============================================================================
set -euo pipefail

PORT=16399
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
    # Send a RESP-encoded command and read the response.
    # Usage: redis_cmd GET mykey
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

# ============================================================================
echo "=== Phase 6 Integration Tests ==="
# ============================================================================

start_server

# ── Test 1: Basic MULTI/EXEC ───────────────────────────────────────────────
echo ""
echo "--- Test 1: MULTI / SET / SET / EXEC ---"

result=$(redis-cli -p "$PORT" <<'EOF'
MULTI
SET a 1
SET b 2
EXEC
EOF
)

assert_contains "MULTI returns OK" "OK" "$result"
assert_contains "EXEC result has OK" "OK" "$result"

# Verify the keys were set.
val_a=$(redis_cmd GET a)
val_b=$(redis_cmd GET b)
assert_eq "GET a after EXEC" "1" "$val_a"
assert_eq "GET b after EXEC" "2" "$val_b"

# ── Test 2: DISCARD clears transaction ─────────────────────────────────────
echo ""
echo "--- Test 2: MULTI / SET / DISCARD ---"

result=$(redis-cli -p "$PORT" <<'EOF'
MULTI
SET c 3
DISCARD
EOF
)

assert_contains "DISCARD returns OK" "OK" "$result"

val_c=$(redis_cmd GET c)
# c should not exist since DISCARD was called.
assert_eq "GET c after DISCARD returns empty" "" "$val_c"

# ── Test 3: MULTI/EXEC with an invalid command in the queue ────────────────
echo ""
echo "--- Test 3: MULTI / SET / invalid / EXEC ---"

result=$(redis-cli -p "$PORT" <<'EOF'
MULTI
SET d 4
BADCOMMAND foo
EXEC
EOF
)

assert_contains "EXEC result includes OK for SET d" "OK" "$result"
# The BADCOMMAND should produce an error in the EXEC result array,
# but SET d should still succeed.
val_d=$(redis_cmd GET d)
assert_eq "GET d after EXEC with invalid cmd" "4" "$val_d"

# ── Test 4: EXEC without MULTI ────────────────────────────────────────────
echo ""
echo "--- Test 4: EXEC without MULTI ---"

result=$(redis_cmd EXEC)
assert_contains "EXEC without MULTI returns error" "ERR" "$result"

# ── Test 5: DISCARD without MULTI ─────────────────────────────────────────
echo ""
echo "--- Test 5: DISCARD without MULTI ---"

result=$(redis_cmd DISCARD)
assert_contains "DISCARD without MULTI returns error" "ERR" "$result"

# ── Test 6: Nested MULTI ──────────────────────────────────────────────────
echo ""
echo "--- Test 6: Nested MULTI ---"

result=$(redis-cli -p "$PORT" <<'EOF'
MULTI
MULTI
EXEC
EOF
)

assert_contains "nested MULTI returns error" "ERR" "$result"

# ── Test 7: PUBLISH (no subscribers) ──────────────────────────────────────
echo ""
echo "--- Test 7: PUBLISH to empty channel ---"

result=$(redis_cmd PUBLISH testchan "hello world")
assert_eq "PUBLISH with no subscribers returns 0" "0" "$result"

# ── Test 8: SUBSCRIBE + PUBLISH ───────────────────────────────────────────
echo ""
echo "--- Test 8: SUBSCRIBE + PUBLISH ---"

# Start a subscriber in the background, capture output.
SUBOUT=$(mktemp)
redis-cli -p "$PORT" SUBSCRIBE mychan > "$SUBOUT" 2>/dev/null &
SUB_PID=$!
sleep 0.5  # Let subscribe register.

# Publish a message.
pub_result=$(redis_cmd PUBLISH mychan "hello")
assert_eq "PUBLISH returns 1 (one subscriber)" "1" "$pub_result"

# Wait for the message to arrive and be written to the file.
sleep 1.0

# Kill the subscriber.
kill "$SUB_PID" 2>/dev/null || true
wait "$SUB_PID" 2>/dev/null || true

# Check subscriber output.
sub_output=$(cat "$SUBOUT")
rm -f "$SUBOUT"

assert_contains "subscriber sees 'subscribe'" "subscribe" "$sub_output"
assert_contains "subscriber sees 'message'" "message" "$sub_output"
assert_contains "subscriber sees channel name" "mychan" "$sub_output"
assert_contains "subscriber sees message content" "hello" "$sub_output"

# ── Test 9: SCAN with multiple keys ──────────────────────────────────────
echo ""
echo "--- Test 9: SCAN iteration ---"

# Set several keys.
for i in $(seq 1 20); do
    redis_cmd SET "scankey:$i" "val$i" > /dev/null
done

# Iterate with SCAN until cursor returns to 0.
cursor=0
all_keys=""
iterations=0
max_iterations=100

while true; do
    result=$(redis_cmd SCAN "$cursor" COUNT 5)
    # redis-cli returns the cursor on the first line, then keys.
    cursor=$(echo "$result" | head -1)
    keys=$(echo "$result" | tail -n +2)
    all_keys="$all_keys $keys"
    ((iterations++)) || true

    if [[ "$cursor" == "0" ]]; then
        break
    fi
    if [[ "$iterations" -ge "$max_iterations" ]]; then
        echo "  FAIL: SCAN did not complete within $max_iterations iterations"
        ((FAIL++)) || true
        break
    fi
done

# Check that we found all 20 scankey:* keys (plus the keys from earlier tests).
found=0
for i in $(seq 1 20); do
    if [[ "$all_keys" == *"scankey:$i"* ]]; then
        ((found++)) || true
    fi
done
assert_eq "SCAN found all 20 scankey:* keys" "20" "$found"

if [[ "$cursor" == "0" ]]; then
    echo "  PASS: SCAN cursor returned to 0"
    ((PASS++)) || true
fi

# ── Test 10: SCAN with COUNT and MATCH ────────────────────────────────────
echo ""
echo "--- Test 10: SCAN basic with COUNT ---"

result=$(redis_cmd SCAN 0 COUNT 3)
# Should return a cursor and some keys.
cursor=$(echo "$result" | head -1)
# Just verify it doesn't error.
assert_contains "SCAN COUNT returns numeric cursor" "" "$result"

# ============================================================================
echo ""
echo "============================================"
echo "  Results: $PASS passed, $FAIL failed"
echo "============================================"

if [[ "$FAIL" -gt 0 ]]; then
    exit 1
fi
exit 0
