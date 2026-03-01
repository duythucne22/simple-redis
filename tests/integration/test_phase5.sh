#!/usr/bin/env bash
# Phase 5 Integration Tests — List, Hash, Set, Sorted Set commands + AOF + WRONGTYPE.
# Requires: redis-cli, simple-redis server binary.
set -euo pipefail

PORT=6399
AOF_FILE="appendonly.aof"
SERVER="./build/simple-redis"
PASS=0
FAIL=0

cleanup() {
    if [[ -n "${SERVER_PID:-}" ]]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -f "$AOF_FILE" temp-rewrite-*.aof
}
trap cleanup EXIT

start_server() {
    rm -f "$AOF_FILE"
    "$SERVER" "$PORT" &
    SERVER_PID=$!
    sleep 0.3
}

stop_server() {
    if [[ -n "${SERVER_PID:-}" ]]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
        unset SERVER_PID
    fi
}

rcli() {
    redis-cli -p "$PORT" "$@" 2>/dev/null
}

check() {
    local desc="$1" expected="$2" actual="$3"
    if [[ "$actual" == "$expected" ]]; then
        printf "  %-55s PASS\n" "$desc"
        PASS=$((PASS + 1))
    else
        printf "  %-55s FAIL\n" "$desc"
        printf "    expected: [%s]\n" "$expected"
        printf "    actual:   [%s]\n" "$actual"
        FAIL=$((FAIL + 1))
    fi
}

check_contains() {
    local desc="$1" expected="$2" actual="$3"
    if [[ "$actual" == *"$expected"* ]]; then
        printf "  %-55s PASS\n" "$desc"
        PASS=$((PASS + 1))
    else
        printf "  %-55s FAIL\n" "$desc"
        printf "    expected to contain: [%s]\n" "$expected"
        printf "    actual:   [%s]\n" "$actual"
        FAIL=$((FAIL + 1))
    fi
}

echo "=== Phase 5 Integration Tests ==="

# ── Start server ───────────────────────────────────────────────────────────
start_server
echo ""
echo "--- List Commands ---"

# RPUSH + LLEN
check "RPUSH mylist a b c" "3" "$(rcli RPUSH mylist a b c)"
check "LLEN mylist" "3" "$(rcli LLEN mylist)"

# LPUSH
check "LPUSH mylist z" "4" "$(rcli LPUSH mylist z)"

# LRANGE full
result=$(rcli LRANGE mylist 0 -1)
check_contains "LRANGE mylist 0 -1 contains z" "z" "$result"
check_contains "LRANGE mylist 0 -1 contains a" "a" "$result"

# LRANGE partial
result=$(rcli LRANGE mylist 0 1)
check_contains "LRANGE mylist 0 1 first is z" "z" "$result"

# LPOP
check "LPOP mylist" "z" "$(rcli LPOP mylist)"
check "LLEN after LPOP" "3" "$(rcli LLEN mylist)"

# RPOP
check "RPOP mylist" "c" "$(rcli RPOP mylist)"

# Pop until empty — key should be auto-deleted
rcli LPOP mylist > /dev/null
rcli LPOP mylist > /dev/null
check "EXISTS after pop all" "0" "$(rcli EXISTS mylist)"

echo ""
echo "--- Hash Commands ---"

# HSET + HGET
check "HSET myhash f1 v1 f2 v2" "2" "$(rcli HSET myhash f1 v1 f2 v2)"
check "HGET myhash f1" "v1" "$(rcli HGET myhash f1)"
check "HGET myhash f2" "v2" "$(rcli HGET myhash f2)"
check "HGET myhash missing" "" "$(rcli HGET myhash missing)"

# HLEN
check "HLEN myhash" "2" "$(rcli HLEN myhash)"

# HSET update existing field
check "HSET myhash f1 updated" "0" "$(rcli HSET myhash f1 updated)"
check "HGET myhash f1 after update" "updated" "$(rcli HGET myhash f1)"

# HGETALL
result=$(rcli HGETALL myhash)
check_contains "HGETALL contains f1" "f1" "$result"
check_contains "HGETALL contains updated" "updated" "$result"

# HDEL
check "HDEL myhash f1" "1" "$(rcli HDEL myhash f1)"
check "HLEN after HDEL" "1" "$(rcli HLEN myhash)"

# HDEL remaining → auto-delete
check "HDEL myhash f2" "1" "$(rcli HDEL myhash f2)"
check "EXISTS myhash after HDEL all" "0" "$(rcli EXISTS myhash)"

echo ""
echo "--- Set Commands ---"

# SADD + SCARD
check "SADD myset a b c" "3" "$(rcli SADD myset a b c)"
check "SCARD myset" "3" "$(rcli SCARD myset)"

# SADD duplicate
check "SADD myset a" "0" "$(rcli SADD myset a)"
check "SCARD after dup" "3" "$(rcli SCARD myset)"

# SISMEMBER
check "SISMEMBER myset a" "1" "$(rcli SISMEMBER myset a)"
check "SISMEMBER myset z" "0" "$(rcli SISMEMBER myset z)"

# SMEMBERS
result=$(rcli SMEMBERS myset)
check_contains "SMEMBERS contains a" "a" "$result"
check_contains "SMEMBERS contains b" "b" "$result"
check_contains "SMEMBERS contains c" "c" "$result"

# SREM
check "SREM myset a" "1" "$(rcli SREM myset a)"
check "SCARD after SREM" "2" "$(rcli SCARD myset)"

# SREM remaining → auto-delete
rcli SREM myset b c > /dev/null
check "EXISTS myset after SREM all" "0" "$(rcli EXISTS myset)"

echo ""
echo "--- Sorted Set Commands ---"

# ZADD + ZCARD
check "ZADD myzset 1 a 2 b 3 c" "3" "$(rcli ZADD myzset 1 a 2 b 3 c)"
check "ZCARD myzset" "3" "$(rcli ZCARD myzset)"

# ZSCORE
check "ZSCORE myzset a" "1" "$(rcli ZSCORE myzset a)"
check "ZSCORE myzset b" "2" "$(rcli ZSCORE myzset b)"

# ZRANK
check "ZRANK myzset a" "0" "$(rcli ZRANK myzset a)"
check "ZRANK myzset c" "2" "$(rcli ZRANK myzset c)"

# ZRANGE
result=$(rcli ZRANGE myzset 0 -1)
check_contains "ZRANGE first element" "a" "$result"

result=$(rcli ZRANGE myzset 0 -1 WITHSCORES)
check_contains "ZRANGE WITHSCORES has score" "1" "$result"

# ZADD update score
check "ZADD myzset 10 a (update)" "0" "$(rcli ZADD myzset 10 a)"
check "ZSCORE myzset a after update" "10" "$(rcli ZSCORE myzset a)"
check "ZRANK myzset a after update" "2" "$(rcli ZRANK myzset a)"

# ZREM
check "ZREM myzset a" "1" "$(rcli ZREM myzset a)"
check "ZCARD after ZREM" "2" "$(rcli ZCARD myzset)"

# ZREM remaining → auto-delete
rcli ZREM myzset b c > /dev/null
check "EXISTS myzset after ZREM all" "0" "$(rcli EXISTS myzset)"

echo ""
echo "--- WRONGTYPE checks ---"

# Set a string key, then try list operations on it.
rcli SET strkey hello > /dev/null
result=$(rcli LPUSH strkey x 2>&1)
check_contains "LPUSH on string key → WRONGTYPE" "WRONGTYPE" "$result"

result=$(rcli SADD strkey x 2>&1)
check_contains "SADD on string key → WRONGTYPE" "WRONGTYPE" "$result"

# Create a list key, then try GET on it.
rcli RPUSH listkey a > /dev/null
result=$(rcli GET listkey 2>&1)
check_contains "GET on list key → WRONGTYPE" "WRONGTYPE" "$result"

echo ""
echo "--- AOF Persistence Test ---"

# Insert data of various types.
rcli SET persist_str "hello" > /dev/null
rcli RPUSH persist_list x y z > /dev/null
rcli HSET persist_hash f1 v1 f2 v2 > /dev/null
rcli SADD persist_set a b c > /dev/null
rcli ZADD persist_zset 1 alpha 2 beta 3 gamma > /dev/null

# Stop the server, restart, and check data survived.
stop_server
sleep 0.2
"$SERVER" "$PORT" &
SERVER_PID=$!
sleep 0.5

check "AOF: persist_str survived" "hello" "$(rcli GET persist_str)"
check "AOF: persist_list LLEN" "3" "$(rcli LLEN persist_list)"
check "AOF: persist_hash HGET f1" "v1" "$(rcli HGET persist_hash f1)"
check "AOF: persist_set SISMEMBER a" "1" "$(rcli SISMEMBER persist_set a)"
check "AOF: persist_zset ZSCORE alpha" "1" "$(rcli ZSCORE persist_zset alpha)"

echo ""
echo "==========================================="
echo "Passed: $PASS   Failed: $FAIL"
if [[ $FAIL -gt 0 ]]; then
    echo "SOME TESTS FAILED"
    exit 1
else
    echo "ALL TESTS PASSED"
fi
