#!/usr/bin/env bash

set -euo pipefail

SERVER=./msgpass_server
CLIENT=./msgpass_client
SOCK=/tmp/msgpass_stress_$$.sock
NUM_CLIENTS=50
REQUESTS_PER_CLIENT=5
FAILED=0
PIDS=()

cleanup() {
    kill "${PIDS[@]}" 2>/dev/null || true
    wait 2>/dev/null || true
    rm -f "$SOCK"
}
trap cleanup EXIT

echo "=== msgpass stress test ==="
echo "Clients : $NUM_CLIENTS"
echo "Requests: $REQUESTS_PER_CLIENT each"
echo ""

# Start single-threaded server.
"$SERVER" -s "$SOCK" &
PIDS+=($!)
sleep 0.3

if ! kill -0 "${PIDS[0]}" 2>/dev/null; then
    echo "ERROR: server failed to start"
    exit 1
fi
echo "Server started (pid=${PIDS[0]})"

run_client() {
    local id=$1
    for _ in $(seq 1 $REQUESTS_PER_CLIENT); do
        "$CLIENT" -s "$SOCK" PWD   >/dev/null 2>&1 || return 1
        "$CLIENT" -s "$SOCK" LS /tmp >/dev/null 2>&1 || return 1
    done
}

WORKER_PIDS=()
for i in $(seq 1 $NUM_CLIENTS); do
    run_client "$i" &
    WORKER_PIDS+=($!)
done

echo "Waiting for $NUM_CLIENTS clients..."
for pid in "${WORKER_PIDS[@]}"; do
    if ! wait "$pid"; then
        FAILED=$((FAILED + 1))
    fi
done

echo ""
if [ $FAILED -eq 0 ]; then
    echo "PASSED: all $NUM_CLIENTS clients completed successfully"
else
    echo "FAILED: $FAILED client(s) reported errors"
fi

kill "${PIDS[0]}" 2>/dev/null
wait "${PIDS[0]}" 2>/dev/null || true

exit $FAILED
