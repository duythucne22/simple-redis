#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# Phase 1 Stress Test — 10,000 Idle Connections
#
# Acceptance criteria (from spec §Phase 1 & understanding doc §9):
#   1. Server holds 10,000 idle TCP connections simultaneously.
#   2. Total RSS stays under 200 MB.
#   3. Server remains responsive after all connections are established.
#
# Prerequisites:
#   - The ulimit for open files must be at least ~10,500 for this script.
#   - Python 3 with the socket module (standard library).
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

PORT=6398
SERVER=./build/simple-redis
NUM_CONNS=10000
MAX_RSS_KB=$((200 * 1024))   # 200 MB in KB
SERVER_PID=""

cleanup() {
    # Kill the Python script if running
    if [[ -n "${PY_PID:-}" ]]; then
        kill "$PY_PID" 2>/dev/null || true
        wait "$PY_PID" 2>/dev/null || true
    fi
    if [[ -n "$SERVER_PID" ]]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
        SERVER_PID=""
    fi
}
trap cleanup EXIT

# ── Ensure the binary exists ────────────────────────────────────────────────
if [[ ! -x "$SERVER" ]]; then
    echo "Building server..."
    make -s
fi

# ── Check fd limit ──────────────────────────────────────────────────────────
FD_LIM=$(ulimit -n)
NEEDED=$((NUM_CONNS + 500))
if [[ "$FD_LIM" -lt "$NEEDED" ]]; then
    echo "WARNING: ulimit -n is $FD_LIM (need ~$NEEDED). Trying to raise..."
    ulimit -n "$NEEDED" 2>/dev/null || true
    FD_LIM=$(ulimit -n)
    if [[ "$FD_LIM" -lt "$NEEDED" ]]; then
        echo "SKIP: Cannot raise fd limit to $NEEDED (got $FD_LIM)."
        echo "Run as root or increase /etc/security/limits.conf."
        exit 0
    fi
fi

# ── Start server ────────────────────────────────────────────────────────────
echo "Starting server on port $PORT..."
$SERVER "$PORT" &
SERVER_PID=$!
sleep 0.5
if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "FATAL: Server failed to start."
    exit 1
fi

# ── Open 10,000 connections using a Python helper ───────────────────────────
echo "Opening $NUM_CONNS idle connections..."

python3 -u - "$PORT" "$NUM_CONNS" <<'PYEOF' &
import socket, sys, time, os

port = int(sys.argv[1])
count = int(sys.argv[2])

socks = []
for i in range(count):
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(("127.0.0.1", port))
        s.setblocking(False)
        socks.append(s)
    except Exception as e:
        print(f"Failed at connection {i}: {e}", file=sys.stderr)
        break
    if (i + 1) % 1000 == 0:
        print(f"  {i + 1} connections opened", flush=True)

print(f"OPENED {len(socks)} connections", flush=True)

# Signal readiness
with open("/tmp/stress_test_ready", "w") as f:
    f.write(str(len(socks)))

# Hold connections open until we receive SIGTERM or stdin closes.
try:
    while True:
        time.sleep(1)
except (KeyboardInterrupt, BrokenPipeError):
    pass
finally:
    for s in socks:
        try:
            s.close()
        except:
            pass
    print(f"Closed {len(socks)} connections", flush=True)
PYEOF
PY_PID=$!

# Wait for the Python script to finish opening connections.
echo "Waiting for connections to be established..."
for attempt in $(seq 1 60); do
    if [[ -f /tmp/stress_test_ready ]]; then
        break
    fi
    sleep 1
done
rm -f /tmp/stress_test_ready

sleep 2  # Let the server settle

# ── Check connection count ──────────────────────────────────────────────────
# Count established connections to the server port.
ESTABLISHED=$(ss -tn state established "( dport = :$PORT )" 2>/dev/null | tail -n +2 | wc -l)
echo "Established connections: $ESTABLISHED"
if [[ "$ESTABLISHED" -ge "$NUM_CONNS" ]]; then
    echo "[PASS] $NUM_CONNS idle connections held"
else
    echo "[FAIL] Only $ESTABLISHED of $NUM_CONNS connections"
fi

# ── Check RSS ───────────────────────────────────────────────────────────────
RSS_KB=$(ps -o rss= -p "$SERVER_PID" 2>/dev/null | tr -d ' ')
RSS_MB=$(( RSS_KB / 1024 ))
echo "Server RSS: ${RSS_MB} MB (${RSS_KB} KB)"
if [[ "$RSS_KB" -le "$MAX_RSS_KB" ]]; then
    echo "[PASS] RSS ${RSS_MB} MB <= 200 MB limit"
else
    echo "[FAIL] RSS ${RSS_MB} MB exceeds 200 MB limit"
fi

# ── Check responsiveness ───────────────────────────────────────────────────
ECHO_RESULT=$(echo "ping_after_10k" | nc -q1 -w2 localhost "$PORT" 2>/dev/null || true)
if [[ "$ECHO_RESULT" == "ping_after_10k" ]]; then
    echo "[PASS] Server still responsive with $NUM_CONNS idle connections"
else
    echo "[FAIL] Server unresponsive (got: '$ECHO_RESULT')"
fi

# ── Cleanup ─────────────────────────────────────────────────────────────────
echo ""
echo "Stress test complete. Cleaning up..."
kill "$PY_PID" 2>/dev/null || true
wait "$PY_PID" 2>/dev/null || true
PY_PID=""
