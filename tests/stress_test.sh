#!/usr/bin/env bash

set -euo pipefail

SERVER="${1:-./msgpass_server}"
CLIENT="${2:-./msgpass_client}"

NUM_CLIENTS=10
REQUESTS_PER_CLIENT=3
TOTAL_ERRORS=0
SERVER_PID=""

cleanup() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -f /tmp/msgpass_stress_*.sock
}
trap cleanup EXIT

pass() { echo "  PASS: $1"; }
fail() { echo "  FAIL: $1"; TOTAL_ERRORS=$((TOTAL_ERRORS + 1)); }

start_server() {
    "$SERVER" "$@" >/dev/null 2>&1 &
    SERVER_PID=$!
}

stop_server() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
        SERVER_PID=""
    fi
    sleep 0.2
}

# Check if server is ready by sending a real request with a timeout
wait_for_server() {
    local mode="$1"
    local addr="$2"
    local tries=30

    echo "  waiting for server..."

    while [ $tries -gt 0 ]; do
        # Check server process is still alive
        if ! kill -0 "$SERVER_PID" 2>/dev/null; then
            echo "  server process died"
            return 1
        fi

        local rc=0
        if [ "$mode" = "unix" ]; then
            timeout 2 "$CLIENT" -s "$addr" PWD >/dev/null 2>&1 || rc=$?
        else
            timeout 2 "$CLIENT" -p "$addr" PWD >/dev/null 2>&1 || rc=$?
        fi

        if [ "$rc" -eq 0 ]; then
            echo "  server ready"
            return 0
        fi

        sleep 0.2
        tries=$((tries - 1))
    done

    echo "  server did not become ready"
    return 1
}

test_pwd() {
    local mode="$1" addr="$2" output=""
    if [ "$mode" = "unix" ]; then
        output=$(timeout 5 "$CLIENT" -s "$addr" PWD 2>/dev/null) || true
    else
        output=$(timeout 5 "$CLIENT" -p "$addr" PWD 2>/dev/null) || true
    fi
    if [ -n "$output" ] && [ "$output" != "Command execution failed" ]; then
        pass "PWD ($mode)"
    else
        fail "PWD ($mode)"
    fi
}

test_ls() {
    local mode="$1" addr="$2" output=""
    if [ "$mode" = "unix" ]; then
        output=$(timeout 5 "$CLIENT" -s "$addr" LS /tmp 2>/dev/null) || true
    else
        output=$(timeout 5 "$CLIENT" -p "$addr" LS /tmp 2>/dev/null) || true
    fi
    if [ -n "$output" ]; then
        pass "LS /tmp ($mode)"
    else
        fail "LS /tmp ($mode)"
    fi
}

test_cat() {
    local mode="$1" addr="$2"
    if [ ! -f /etc/hostname ]; then
        echo "  SKIP: CAT (no /etc/hostname)"
        return
    fi
    local expected got=""
    expected=$(cat /etc/hostname)
    if [ "$mode" = "unix" ]; then
        got=$(timeout 5 "$CLIENT" -s "$addr" CAT /etc/hostname 2>/dev/null) || true
    else
        got=$(timeout 5 "$CLIENT" -p "$addr" CAT /etc/hostname 2>/dev/null) || true
    fi
    if [ "$expected" = "$got" ]; then
        pass "CAT /etc/hostname ($mode)"
    else
        fail "CAT /etc/hostname ($mode)"
    fi
}

test_stdin() {
    local mode="$1" addr="$2" output=""
    if [ "$mode" = "unix" ]; then
        output=$(printf 'PWD\nLS /tmp\n' | timeout 5 "$CLIENT" -s "$addr" 2>/dev/null) || true
    else
        output=$(printf 'PWD\nLS /tmp\n' | timeout 5 "$CLIENT" -p "$addr" 2>/dev/null) || true
    fi
    if [ -n "$output" ]; then
        pass "stdin batch ($mode)"
    else
        fail "stdin batch ($mode)"
    fi
}

run_stress() {
    local mode="$1" addr="$2"
    local failed=0
    local pids=()

    for i in $(seq 1 "$NUM_CLIENTS"); do
        (
            errs=0
            for j in $(seq 1 "$REQUESTS_PER_CLIENT"); do
                if [ "$mode" = "unix" ]; then
                    timeout 5 "$CLIENT" -s "$addr" PWD     >/dev/null 2>&1 || errs=$((errs+1))
                    timeout 5 "$CLIENT" -s "$addr" LS /tmp >/dev/null 2>&1 || errs=$((errs+1))
                else
                    timeout 5 "$CLIENT" -p "$addr" PWD     >/dev/null 2>&1 || errs=$((errs+1))
                    timeout 5 "$CLIENT" -p "$addr" LS /tmp >/dev/null 2>&1 || errs=$((errs+1))
                fi
            done
            exit $errs
        ) &
        pids+=($!)
    done

    for pid in "${pids[@]}"; do
        wait "$pid" 2>/dev/null || failed=$((failed + 1))
    done

    if [ "$failed" -eq 0 ]; then
        pass "stress ($NUM_CLIENTS clients x $REQUESTS_PER_CLIENT req)"
    else
        fail "stress: $failed/$NUM_CLIENTS clients had errors"
    fi
}

run_scenario() {
    local label="$1"
    local mode="$2"
    local addr="$3"
    shift 3

    echo ""
    echo "==== $label ===="

    start_server "$@"
    sleep 0.5

    if ! wait_for_server "$mode" "$addr"; then
        fail "server did not start"
        stop_server
        return
    fi

    test_pwd   "$mode" "$addr"
    test_ls    "$mode" "$addr"
    test_cat   "$mode" "$addr"
    test_stdin "$mode" "$addr"
    run_stress "$mode" "$addr"

    stop_server
}

# Kill any leftover servers from previous runs
killall msgpass_server 2>/dev/null || true
sleep 0.3

SOCK_ST="/tmp/msgpass_stress_st_$$.sock"
SOCK_MT="/tmp/msgpass_stress_mt_$$.sock"

run_scenario "Scenario 1: single-threaded + UNIX" \
    "unix" "$SOCK_ST" -s "$SOCK_ST"
rm -f "$SOCK_ST"

run_scenario "Scenario 2: single-threaded + TCP" \
    "tcp" "19871" -p 19871
sleep 0.5

run_scenario "Scenario 3: multi-threaded + UNIX" \
    "unix" "$SOCK_MT" -s "$SOCK_MT" -t
rm -f "$SOCK_MT"

run_scenario "Scenario 4: multi-threaded + TCP" \
    "tcp" "19872" -p 19872 -t

echo ""
echo "==============================="
if [ "$TOTAL_ERRORS" -eq 0 ]; then
    echo "ALL TESTS PASSED"
else
    echo "$TOTAL_ERRORS TEST(S) FAILED"
fi
echo "==============================="

exit "$TOTAL_ERRORS"