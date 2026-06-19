#!/usr/bin/env bash
# stress_test.sh — Launch concurrent clients against each server mode
# and verify that all requests complete without error.

set -euo pipefail

# These must point to built binaries
SERVER="${1:-./msgpass_server}"
CLIENT="${2:-./msgpass_client}"

NUM_CLIENTS=50
REQUESTS_PER_CLIENT=5
TOTAL_ERRORS=0

# Colours (skip if not a terminal)
if [ -t 1 ]; then
    RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
else
    RED=''; GREEN=''; YELLOW=''; NC=''
fi

pass() { printf "${GREEN}PASS${NC} %s\n" "$1"; }
fail() { printf "${RED}FAIL${NC} %s\n" "$1"; TOTAL_ERRORS=$((TOTAL_ERRORS + 1)); }
info() { printf "${YELLOW}----${NC} %s\n" "$1"; }

# ── helpers ──────────────────────────────────────────────────────────────────

start_server() {
    local args=("$@")
    "$SERVER" "${args[@]}" &
    echo $!
}

stop_server() {
    local pid=$1
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
}

run_clients() {
    local mode="$1"   # "unix" or "tcp"
    local addr="$2"   # socket path or port number
    local failed=0
    local pids=()

    for i in $(seq 1 "$NUM_CLIENTS"); do
        (
            local errs=0
            for _ in $(seq 1 "$REQUESTS_PER_CLIENT"); do
                if [ "$mode" = "unix" ]; then
                    "$CLIENT" -s "$addr" PWD      >/dev/null 2>&1 || errs=$((errs+1))
                    "$CLIENT" -s "$addr" LS /tmp  >/dev/null 2>&1 || errs=$((errs+1))
                else
                    "$CLIENT" -p "$addr" PWD      >/dev/null 2>&1 || errs=$((errs+1))
                    "$CLIENT" -p "$addr" LS /tmp  >/dev/null 2>&1 || errs=$((errs+1))
                fi
            done
            exit $errs
        ) &
        pids+=($!)
    done

    for pid in "${pids[@]}"; do
        if ! wait "$pid"; then
            failed=$((failed + 1))
        fi
    done

    echo "$failed"
}

wait_for_unix_socket() {
    local path="$1"
    local retries=20
    while [ $retries -gt 0 ]; do
        [ -S "$path" ] && return 0
        sleep 0.1
        retries=$((retries - 1))
    done
    return 1
}

wait_for_tcp_port() {
    local port="$1"
    local retries=20
    while [ $retries -gt 0 ]; do
        if 2>/dev/null bash -c "echo > /dev/tcp/127.0.0.1/$port"; then
            return 0
        fi
        sleep 0.1
        retries=$((retries - 1))
    done
    return 1
}

# ── test cases ────────────────────────────────────────────────────────────────

test_stdin_mode() {
    local mode="$1"
    local addr="$2"

    local output
    if [ "$mode" = "unix" ]; then
        output=$(printf 'PWD\nLS /tmp\n' | "$CLIENT" -s "$addr" 2>/dev/null)
    else
        output=$(printf 'PWD\nLS /tmp\n' | "$CLIENT" -p "$addr" 2>/dev/null)
    fi

    if [ -n "$output" ]; then
        pass "stdin mode ($mode)"
    else
        fail "stdin mode ($mode) — no output"
    fi
}

test_cat() {
    local mode="$1"
    local addr="$2"
    local target="/etc/hostname"

    [ -f "$target" ] || { info "skipping CAT test (no $target)"; return; }

    local expected
    expected=$(cat "$target")

    local got
    if [ "$mode" = "unix" ]; then
        got=$("$CLIENT" -s "$addr" CAT "$target" 2>/dev/null)
    else
        got=$("$CLIENT" -p "$addr" CAT "$target" 2>/dev/null)
    fi

    if [ "$expected" = "$got" ]; then
        pass "CAT /etc/hostname ($mode)"
    else
        fail "CAT /etc/hostname ($mode) — output mismatch"
    fi
}

# ── scenario 1: single-threaded + UNIX socket ─────────────────────────────

SOCK="/tmp/msgpass_stress_st_$$.sock"
info "Scenario 1: single-threaded + UNIX socket ($NUM_CLIENTS clients × $REQUESTS_PER_CLIENT req)"

SERVER_PID=$(start_server -s "$SOCK")
if ! wait_for_unix_socket "$SOCK"; then
    fail "server did not start (UNIX/ST)"
    stop_server "$SERVER_PID"
else
    errors=$(run_clients "unix" "$SOCK")
    if [ "$errors" -eq 0 ]; then
        pass "single-threaded UNIX: all $NUM_CLIENTS clients OK"
    else
        fail "single-threaded UNIX: $errors client(s) reported errors"
    fi

    test_stdin_mode "unix" "$SOCK"
    test_cat "unix" "$SOCK"
    stop_server "$SERVER_PID"
fi
rm -f "$SOCK"

# ── scenario 2: single-threaded + TCP ────────────────────────────────────

TCP_PORT=19871
info "Scenario 2: single-threaded + TCP ($NUM_CLIENTS clients × $REQUESTS_PER_CLIENT req)"

SERVER_PID=$(start_server -p "$TCP_PORT")
if ! wait_for_tcp_port "$TCP_PORT"; then
    fail "server did not start (TCP/ST)"
    stop_server "$SERVER_PID"
else
    errors=$(run_clients "tcp" "$TCP_PORT")
    if [ "$errors" -eq 0 ]; then
        pass "single-threaded TCP: all $NUM_CLIENTS clients OK"
    else
        fail "single-threaded TCP: $errors client(s) reported errors"
    fi

    test_stdin_mode "tcp" "$TCP_PORT"
    test_cat "tcp" "$TCP_PORT"
    stop_server "$SERVER_PID"
fi

# ── scenario 3: multi-threaded + UNIX socket ──────────────────────────────

SOCK="/tmp/msgpass_stress_mt_$$.sock"
info "Scenario 3: multi-threaded + UNIX socket ($NUM_CLIENTS clients × $REQUESTS_PER_CLIENT req)"

SERVER_PID=$(start_server -s "$SOCK" -t)
if ! wait_for_unix_socket "$SOCK"; then
    fail "server did not start (UNIX/MT)"
    stop_server "$SERVER_PID"
else
    errors=$(run_clients "unix" "$SOCK")
    if [ "$errors" -eq 0 ]; then
        pass "multi-threaded UNIX: all $NUM_CLIENTS clients OK"
    else
        fail "multi-threaded UNIX: $errors client(s) reported errors"
    fi
    stop_server "$SERVER_PID"
fi
rm -f "$SOCK"

# ── scenario 4: multi-threaded + TCP ─────────────────────────────────────

TCP_PORT=19872
info "Scenario 4: multi-threaded + TCP ($NUM_CLIENTS clients × $REQUESTS_PER_CLIENT req)"

SERVER_PID=$(start_server -p "$TCP_PORT" -t)
if ! wait_for_tcp_port "$TCP_PORT"; then
    fail "server did not start (TCP/MT)"
    stop_server "$SERVER_PID"
else
    errors=$(run_clients "tcp" "$TCP_PORT")
    if [ "$errors" -eq 0 ]; then
        pass "multi-threaded TCP: all $NUM_CLIENTS clients OK"
    else
        fail "multi-threaded TCP: $errors client(s) reported errors"
    fi
    stop_server "$SERVER_PID"
fi

# ── summary ───────────────────────────────────────────────────────────────

echo ""
if [ "$TOTAL_ERRORS" -eq 0 ]; then
    printf "${GREEN}All stress tests passed.${NC}\n"
else
    printf "${RED}%d test(s) failed.${NC}\n" "$TOTAL_ERRORS"
fi

exit "$TOTAL_ERRORS"