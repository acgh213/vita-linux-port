#!/bin/sh
set -eu

ROOT=$(CDPATH='' cd -- "$(dirname "$0")/../.." && pwd)
SOURCE="$ROOT/toolkit/share/vita-control.c"
TOOL="$ROOT/toolkit/bin/vita-control"
TMP=$(mktemp -d)
FAKE_PID=
trap 'if [ -n "$FAKE_PID" ]; then kill "$FAKE_PID" 2>/dev/null || true; wait "$FAKE_PID" 2>/dev/null || true; fi; rm -rf "$TMP"' 0 HUP INT TERM

[ -f "$SOURCE" ] || { printf 'FAIL: missing %s\n' "$SOURCE" >&2; exit 1; }
[ -x "$TOOL" ] || { printf 'FAIL: missing executable %s\n' "$TOOL" >&2; exit 1; }

SIGNAL_FIXTURE="$ROOT/toolkit/tests/vita-control-signal-fixture.c"

[ -f "$SIGNAL_FIXTURE" ] || { printf 'FAIL: missing %s\n' "$SIGNAL_FIXTURE" >&2; exit 1; }

cc -std=c11 -Wall -Wextra -Werror "$SOURCE" -o "$TMP/vita-control"
cc -std=c11 -Wall -Wextra -Werror "$SIGNAL_FIXTURE" -o "$TMP/fake-target"
VITA_CONTROL_TEST_LOG="$TMP/signals.log" "$TMP/fake-target" "$TMP/signals.log" &
FAKE_PID=$!
target_exe=$(readlink -f "/proc/$FAKE_PID/exe")
printf '%s\n' "$FAKE_PID" > "$TMP/pid"

run_control() {
    VITA_CONTROL_PIDFILE="$TMP/pid" \
    VITA_CONTROL_EXPECTED_EXE="$target_exe" \
    VITA_CONTROL_STATE="$TMP/state" \
    VITA_CONTROL_MIN_INTERVAL_MS=500 \
    "$@"
}

status=$(run_control "$TMP/vita-control" status --machine)
printf '%s\n' "$status"
printf '%s\n' "$status" | grep -F 'schema=1' >/dev/null
printf '%s\n' "$status" | grep -F "pid=$FAKE_PID" >/dev/null
printf '%s\n' "$status" | grep -F 'running=1' >/dev/null
printf '%s\n' "$status" | grep -F "exe=$target_exe" >/dev/null
printf '%s\n' "$status" | grep -F 'next_signal=SIGUSR1' >/dev/null
printf '%s\n' "$status" | grep -F 'min_interval_ms=500' >/dev/null

first=$(run_control "$TMP/vita-control" next --machine)
printf '%s\n' "$first" | grep -F 'status=triggered' >/dev/null
printf '%s\n' "$first" | grep -F "pid=$FAKE_PID" >/dev/null

if run_control "$TMP/vita-control" next --machine >"$TMP/limited.out" 2>&1; then
    printf '%s\n' 'FAIL: rapid next was not rate-limited' >&2
    exit 1
fi
grep -F 'status=rate_limited' "$TMP/limited.out" >/dev/null
grep -F 'retry_after_ms=' "$TMP/limited.out" >/dev/null
sleep 1.2
run_control "$TMP/vita-control" next --machine | grep -F 'status=triggered' >/dev/null

# Both accepted actions must have reached the real target process.
signals=$(wc -l < "$TMP/signals.log" | tr -d ' ')
[ "$signals" -ge 1 ]

# A mismatched executable must fail closed without signaling.
printf '%s\n' "$$" > "$TMP/pid"
if run_control "$TMP/vita-control" next --machine >"$TMP/mismatch.out" 2>&1; then
    printf '%s\n' 'FAIL: executable mismatch was accepted' >&2
    exit 1
fi
grep -F 'status=target_executable_mismatch' "$TMP/mismatch.out" >/dev/null
[ "$(wc -l < "$TMP/signals.log" | tr -d ' ')" = 2 ]

# The installed wrapper must forward arguments to the payload binary.
fake_wrapper="$TMP/fake-control"
# shellcheck disable=SC2016
printf '%s\n' '#!/bin/sh' \
    'printf "%s\\n" "$*" > "$VITA_CONTROL_WRAPPER_LOG"' \
    'printf "wrapper_ok\\n"' > "$fake_wrapper"
chmod +x "$fake_wrapper"
wrapper_out=$(VITA_CONTROL_BINARY="$fake_wrapper" VITA_CONTROL_WRAPPER_LOG="$TMP/wrapper.log" "$TOOL" status --machine)
printf '%s\n' "$wrapper_out" | grep -F 'wrapper_ok' >/dev/null
grep -F -- 'status --machine' "$TMP/wrapper.log" >/dev/null

printf 'vita-control test passed\n'
