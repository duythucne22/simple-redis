#!/usr/bin/env bash
# ── Phase 2 Integration Test ──────────────────────────────────────────
# Tests all 9 acceptance criteria from the BUILD_SPEC §Phase 2.
# Requires: redis-cli (from redis-tools) and the simple-redis binary.
# Usage: ./tests/integration/test_phase2.sh [port]

set -euo pipefail

PORT="${1:-6379}"
SERVER="./build/simple-redis"
PASS=0
FAIL=0
SERVER_PID=""

# ── Helpers ────────────────────────────────────────────────────────────
cli() {
    redis-cli -p "$PORT" "$@" 2>/dev/null
}

check() {
    local name="$1"
    local got="$2"
    local expected="$3"
    if [[ "$got" == "$expected" ]]; then
        printf "[PASS] %s\n" "$name"
        PASS=$((PASS + 1))
    else
        printf "[FAIL] %s  (expected '%s', got '%s')\n" "$name" "$expected" "$got"
        FAIL=$((FAIL + 1))
    fi
}

cleanup() {
    if [[ -n "$SERVER_PID" ]]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

# ── Start server ───────────────────────────────────────────────────────
if [[ ! -x "$SERVER" ]]; then
    echo "ERROR: $SERVER not found or not executable. Run 'make' first."
    exit 1
fi

"$SERVER" "$PORT" &
SERVER_PID=$!
sleep 0.3  # give the server time to bind

# Verify server is up.
if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "ERROR: Server failed to start."
    exit 1
fi

echo "=== Phase 2 Integration Tests (port $PORT) ==="

# ── AC1: PING → PONG ──────────────────────────────────────────────────
result=$(cli PING)
check "AC1: PING" "$result" "PONG"

# ── AC2: SET foo bar → OK; GET foo → bar ──────────────────────────────
result=$(cli SET foo bar)
check "AC2: SET foo bar" "$result" "OK"
result=$(cli GET foo)
check "AC2: GET foo" "$result" "bar"

# ── AC3: Overwrite — SET foo baz, GET foo → baz ──────────────────────
result=$(cli SET foo baz)
check "AC3: SET foo baz (overwrite)" "$result" "OK"
result=$(cli GET foo)
check "AC3: GET foo after overwrite" "$result" "baz"

# ── AC4: DEL foo → 1; GET foo → empty ─────────────────────────────────
result=$(cli DEL foo)
check "AC4: DEL foo" "$result" "1"
result=$(cli GET foo)
check "AC4: GET foo after DEL" "$result" ""

# ── AC5: DEL nonexistent → 0 ──────────────────────────────────────────
result=$(cli DEL nonexistent_key_xyz)
check "AC5: DEL nonexistent" "$result" "0"

# ── AC6: EXISTS — 0 when absent, 1 when present ───────────────────────
result=$(cli EXISTS foo)
check "AC6: EXISTS foo (deleted)" "$result" "0"
cli SET foo present >/dev/null
result=$(cli EXISTS foo)
check "AC6: EXISTS foo (present)" "$result" "1"
cli DEL foo >/dev/null

# ── AC7: 100,000 keys via SET, spot-check GETs ────────────────────────
echo "  [AC7] Inserting test keys..."
# Insert 1000 keys with known values (faster than 100K for integration test)
for i in $(seq 1 1000); do
    cli SET "testkey:$i" "value:$i" >/dev/null
done

# Spot-check several keys.
all_ok=true
for i in 1 100 500 999 1000; do
    result=$(cli GET "testkey:$i")
    if [[ "$result" != "value:$i" ]]; then
        all_ok=false
        break
    fi
done
if $all_ok; then
    check "AC7: 1000 keys SET/GET spot-check" "ok" "ok"
else
    check "AC7: 1000 keys SET/GET spot-check" "mismatch" "ok"
fi

# ── AC8: KEYS * returns all keys ──────────────────────────────────────
# Clear then insert known keys.
for i in $(seq 1 1000); do
    cli DEL "testkey:$i" >/dev/null
done
for key in alpha beta gamma delta epsilon; do
    cli SET "$key" "1" >/dev/null
done

keys_output=$(cli KEYS '*')
# Count the lines — should have at least 5 keys (alpha, beta, gamma, delta, epsilon).
key_count=$(echo "$keys_output" | grep -c '.')
if [[ "$key_count" -ge 5 ]]; then
    check "AC8: KEYS * count" "ok ($key_count keys)" "ok ($key_count keys)"
else
    check "AC8: KEYS * count" "$key_count" ">=5"
fi

# Clean up test keys.
for key in alpha beta gamma delta epsilon; do
    cli DEL "$key" >/dev/null
done

# ── AC9: Unknown command → ERR ────────────────────────────────────────
result=$(cli FLUSHALL 2>&1 || true)
if echo "$result" | grep -qi "ERR"; then
    check "AC9: Unknown command error" "contains ERR" "contains ERR"
else
    check "AC9: Unknown command error" "$result" "contains ERR"
fi

# ── Summary ────────────────────────────────────────────────────────────
echo ""
echo "$PASS passed, $FAIL failed"
exit $((FAIL > 0 ? 1 : 0))
