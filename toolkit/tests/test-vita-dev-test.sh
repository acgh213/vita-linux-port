#!/bin/sh
set -eu

ROOT=$(CDPATH='' cd -- "$(dirname "$0")/../.." && pwd)
TOOL="$ROOT/toolkit/bin/vita-dev"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' 0 HUP INT TERM

[ -x "$TOOL" ] || { printf 'FAIL: missing executable %s\n' "$TOOL" >&2; exit 1; }

mkdir -p "$TMP/src"
printf '%s\n' \
    '#include "status.h"' \
    '#include <stdio.h>' \
    'int main(void) {' \
    '    printf("test-value:%d\\n", test_status());' \
    '    fflush(stdout);' \
    '    return test_status();' \
    '}' > "$TMP/src/main.c"
printf '%s\n' \
    '#ifndef STATUS_H' \
    '#define STATUS_H' \
    'int test_status(void);' \
    '#endif' > "$TMP/src/status.h"
printf '%s\n' \
    '#include "status.h"' \
    'int test_status(void) { return 7; }' > "$TMP/src/status.c"

PASS=$(VITA_DEV_CC=cc "$TOOL" test --machine --workdir "$TMP/pass" \
    --output passing --expect-status 7 \
    "$TMP/src/main.c" "$TMP/src/status.c")
printf '%s\n' "$PASS"
printf '%s\n' "$PASS" | grep -F 'schema=1' >/dev/null
printf '%s\n' "$PASS" | grep -F 'action=test' >/dev/null
printf '%s\n' "$PASS" | grep -F 'status=pass' >/dev/null
printf '%s\n' "$PASS" | grep -F 'source_count=2' >/dev/null
printf '%s\n' "$PASS" | grep -F 'expected_status=7' >/dev/null
printf '%s\n' "$PASS" | grep -F 'run_status=7' >/dev/null
[ -f "$TMP/pass/.vita-dev-test-output" ]
grep -F 'test-value:7' "$TMP/pass/.vita-dev-test-output" >/dev/null

set +e
FAIL_OUTPUT=$(VITA_DEV_CC=cc "$TOOL" test --machine --workdir "$TMP/fail" \
    --output failing --expect-status 0 \
    "$TMP/src/main.c" "$TMP/src/status.c" 2>&1)
FAIL_STATUS=$?
set -e
[ "$FAIL_STATUS" -ne 0 ]
printf '%s\n' "$FAIL_OUTPUT"
printf '%s\n' "$FAIL_OUTPUT" | grep -F 'status=fail' >/dev/null
printf '%s\n' "$FAIL_OUTPUT" | grep -F 'expected_status=0' >/dev/null
printf '%s\n' "$FAIL_OUTPUT" | grep -F 'run_status=7' >/dev/null

printf 'vita-dev test assertion test passed\n'
