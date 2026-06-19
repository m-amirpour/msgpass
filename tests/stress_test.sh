#!/usr/bin/env bash
#
# stress_test.sh — Run concurrent clients against all four server modes.
#
# Usage:
#   ./stress_test.sh ./msgpass_server ./msgpass_client
#

set -euo pipefail

SERVER="${1:-./msgpass_server}"
CLIENT="${2:-./msgpass_client}"

NUM_CLIENTS=50
REQUESTS_PER_CLIENT=5
TOTAL_ERRORS=0

SERVER_PID=""

cleanup() {
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -f /tmp/msgpass_stress_*.sock
}
trap cleanup EXIT

pass() { echo "  PASS: $1"; }
fail() { echo "  FAIL: $1"; TOTAL_ERRORS=$((TOTAL_ERRORS + 1)); }

# ── server lifecycle ─────────────────────────────────────────────────────

start_server() {
    # Redirect server output to /dev/null so it doesn't pollute test output
    "$SERVER" "$@" >/dev/null 2>&1 &
    SERVER_PID=$!
    sleep 0.3
}

stop_server() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
        SERVER_PID=""
    fi
    sleep 0.2
}

# ── readiness checks ────────────────────────────────────────────────────
# Instead of checking for socket files or port availability using bash
# builtins, we just try to send an actual request. This is the most
# reliable way to know the server is genuinely ready.

wait_for_server() {
    local mode="$1"   # "unix" or "tcp"
    local addr="$2"   # socket path or port
    local tries=50

    while [ $tries -gt 0 ]; do
        local output=""
        if [ "$mode" = "unix" ]; then
            output=$("$CLIENT" -s "$addr" PWD 2>/dev/null) || true
        else
            output=$("$CLIENT" -p "$addr" PWD 2>/dev/null) || true
        fi

        # If we got any real output (not an error message), server is ready
        if [ -n "$output" ] && [ "$output" != "Command execution failed" ]; then
            return 0
        fi

        sleep 0.1
        tries=$((tries - 1))
    done

    return 1
}

# ── functional tests ─────────────────────────────────────────────────────

test_pwd() {
    local mode="$1"
    local addr="$2"
    local output=""

    if [ "$mode" = "unix" ]; then
        output=$("$CLIENT" -s "$addr" PWD 2>/dev/null) || true
    else
        output=$("$CLIENT" -p "$addr" PWD 2>/dev/null) || true
    fi

    if [ -n "$output" ] && [ "$output" != "Command execution failed" ]; then
        pass "PWD ($mode)"
    else
        fail "PWD ($mode) — got: '$output'"
    fi
}

test_ls() {
    local mode="$1"
    local addr="$2"
    local output=""

    if [ "$mode" = "unix" ]; then
        output=$("$CLIENT" -s "$addr" LS /tmp 2>/dev/null) || true
    else
        output=$("$CLIENT" -p "$addr" LS /tmp 2>/dev/null) || true
    fi

    if [ -n "$output" ]; then
        pass "LS /tmp ($mode)"
    else
        fail "LS /tmp ($mode) — no output"
    fi
}

test_cat() {
    local mode="$1"
    local addr="$2"
    local target="/etc/hostname"

    if [ ! -f "$target" ]; then
        echo "  SKIP: CAT test — $target not found"
        return
    fi

    local expected got
    expected=$(cat "$target")
    got=""

    if [ "$mode" = "unix" ]; then
        got=$("$CLIENT" -s "$addr" CAT "$target" 2>/dev/null) || true
    else
        got=$("$CLIENT" -p "$addr" CAT "$target" 2>/dev/null) || true
    fi

    if [ "$expected" = "$got" ]; then
        pass "CAT $target ($mode)"
    else
        fail "CAT $target ($mode) — output mismatch"
    fi
}

test_stdin() {
    local mode="$1"
    local addr="$2"
    local output=""

    if [ "$mode" = "unix" ]; then
        output=$(printf 'PWD\nLS /tmp\n' | "$CLIENT" -s "$addr" 2>/dev/null) || true
    else
        output=$(printf 'PWD\nLS /tmp\n' | "$CLIENT" -p "$addr" 2>/dev/null) || true
    fi

    if [ -n "$output" ]; then
        pass "stdin batch ($mode)"
    else
        fail "stdin batch ($mode) — no output"
    fi
}

run_functional_tests() {
    local mode="$1"
    local addr="$2"

    test_pwd   "$mode" "$addr"
    test_ls    "$mode" "$addr"
    test_cat   "$mode" "$addr"
    test_stdin "$mode" "$addr"
}

# ── stress test ──────────────────────────────────────────────────────────

run_stress() {
    local mode="$1"
    local addr="$2"
    local failed=0
    local pids=()

    for i in $(seq 1 "$NUM_CLIENTS"); do
        (
            errs=0
            for j in $(seq 1 "$REQUESTS_PER_CLIENT"); do
                if [ "$mode" = "unix" ]; then
                    "$CLIENT" -s "$addr" PWD     >/dev/null 2>&1 || errs=$((errs+1))
                    "$CLIENT" -s "$addr" LS /tmp >/dev/null 2>&1 || errs=$((errs+1))
                else
                    "$CLIENT" -p "$addr" PWD     >/dev/null 2>&1 || errs=$((errs+1))
                    "$CLIENT" -p "$addr" LS /tmp >/dev/null 2>&1 || errs=$((errs+1))
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
        pass "stress ($NUM_CLIENTS clients x $REQUESTS_PER_CLIENT req): all OK"
    else
        fail "stress: $failed client(s) failed"
    fi
}

# ── run one scenario ─────────────────────────────────────────────────────

run_scenario() {
    local label="$1"
    local mode="$2"      # "unix" or "tcp"
    local addr="$3"      # socket path or port
    shift 3
    local server_args=("$@")

    echo ""
    echo "==== $label ===="

    start_server "${server_args[@]}"

    if ! wait_for_server "$mode" "$addr"; then
        fail "server did not start"
        stop_server
        return
    fi

    run_functional_tests "$mode" "$addr"
    run_stress "$mode" "$addr"

    stop_server
}

# ── scenarios ────────────────────────────────────────────────────────────

SOCK_ST="/tmp/msgpass_stress_st_$$.sock"
SOCK_MT="/tmp/msgpass_stress_mt_$$.sock"
TCP_PORT_ST=19871
TCP_PORT_MT=19872

run_scenario \
    "Scenario 1: single-threaded + UNIX" \
    "unix" "$SOCK_ST" \
    -s "$SOCK_ST"

rm -f "$SOCK_ST"

run_scenario \
    "Scenario 2: single-threaded + TCP" \
    "tcp" "$TCP_PORT_ST" \
    -p "$TCP_PORT_ST"

run_scenario \
    "Scenario 3: multi-threaded + UNIX" \
    "unix" "$SOCK_MT" \
    -s "$SOCK_MT" -t

rm -f "$SOCK_MT"

run_scenario \
    "Scenario 4: multi-threaded + TCP" \
    "tcp" "$TCP_PORT_MT" \
    -p "$TCP_PORT_MT" -t

# ── summary ──────────────────────────────────────────────────────────────

echo ""
echo "==============================="
if [ "$TOTAL_ERRORS" -eq 0 ]; then
    echo "All tests passed."
else
    echo "$TOTAL_ERRORS test(s) FAILED."
fi
echo "==============================="

exit "$TOTAL_ERRORS"