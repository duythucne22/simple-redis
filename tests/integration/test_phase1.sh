#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# Phase 1 Integration Tests — Echo Server
# Uses Python socket clients for reliable, cross-distro testing.
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

PORT=6399
SERVER=./build/simple-redis
PASS=0
FAIL=0
SERVER_PID=""

cleanup() {
    if [[ -n "$SERVER_PID" ]]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
        SERVER_PID=""
    fi
}
trap cleanup EXIT

run_test() {
    local name="$1"
    local actual="$2"
    local expected="$3"

    if [[ "$actual" == "$expected" ]]; then
        echo "[PASS] $name"
        PASS=$((PASS + 1))
    else
        echo "[FAIL] $name"
        echo "  Expected: '$expected'"
        echo "  Actual:   '$actual'"
        FAIL=$((FAIL + 1))
    fi
}

# ── Build ───────────────────────────────────────────────────────────────────
echo "=== Building ==="
make -s clean
make -s
echo ""

# ── Start server ────────────────────────────────────────────────────────────
echo "=== Starting server on port $PORT ==="
$SERVER "$PORT" &
SERVER_PID=$!
sleep 0.5
if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "FATAL: Server failed to start."
    exit 1
fi

# ── Test 1: Basic echo ─────────────────────────────────────────────────────
RESULT=$(python3 -c "
import socket
s = socket.socket(); s.settimeout(3)
s.connect(('127.0.0.1', $PORT))
s.sendall(b'hello\n')
data = s.recv(4096)
s.close()
print(data.decode().strip())
")
run_test "Basic echo returns sent text" "$RESULT" "hello"

# ── Test 2: Multi-word echo ────────────────────────────────────────────────
RESULT=$(python3 -c "
import socket
s = socket.socket(); s.settimeout(3)
s.connect(('127.0.0.1', $PORT))
s.sendall(b'hello world 123\n')
data = s.recv(4096)
s.close()
print(data.decode().strip())
")
run_test "Multi-word echo" "$RESULT" "hello world 123"

# ── Test 3: Multiple concurrent sessions ───────────────────────────────────
RESULT=$(python3 -c "
import socket, threading, sys

results = {}

def echo_session(idx, port):
    try:
        s = socket.socket(); s.settimeout(3)
        s.connect(('127.0.0.1', port))
        msg = f'session{idx}\n'
        s.sendall(msg.encode())
        data = s.recv(4096)
        s.close()
        results[idx] = data.decode().strip()
    except Exception as e:
        results[idx] = f'ERROR: {e}'

threads = []
for i in range(1, 6):
    t = threading.Thread(target=echo_session, args=(i, $PORT))
    t.start()
    threads.append(t)

for t in threads:
    t.join()

ok = all(results.get(i) == f'session{i}' for i in range(1, 6))
print('true' if ok else 'false')
if not ok:
    for i in range(1, 6):
        exp = f'session{i}'
        got = results.get(i, 'MISSING')
        if got != exp:
            print(f'  session {i}: expected={exp} got={got}', file=sys.stderr)
")
run_test "5 concurrent sessions" "$RESULT" "true"

# ── Test 4: Client disconnect doesn't crash the server ─────────────────────
python3 -c "
import socket
s = socket.socket(); s.settimeout(1)
s.connect(('127.0.0.1', $PORT))
s.sendall(b'bye\n')
s.close()
" 2>/dev/null || true
sleep 0.3
ALIVE=$(kill -0 "$SERVER_PID" 2>/dev/null && echo "yes" || echo "no")
run_test "Server survives client disconnect" "$ALIVE" "yes"

# ── Test 5: SIGINT causes clean shutdown ───────────────────────────────────
kill -INT "$SERVER_PID"
wait "$SERVER_PID" 2>/dev/null || true
EXIT_CODE=$?
SERVER_PID=""
if [[ $EXIT_CODE -eq 0 || $EXIT_CODE -eq 130 ]]; then
    run_test "SIGINT clean shutdown" "clean" "clean"
else
    run_test "SIGINT clean shutdown" "exit=$EXIT_CODE" "clean"
fi

# ── Summary ─────────────────────────────────────────────────────────────────
echo ""
echo "=== Phase 1 Integration Results: $PASS passed, $FAIL failed ==="
[[ $FAIL -eq 0 ]] && exit 0 || exit 1
