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
    '    printf("manifest-value:%d\\n", manifest_status());' \
    '    fflush(stdout);' \
    '    return manifest_status();' \
    '}' > "$TMP/src/main.c"
printf '%s\n' \
    '#ifndef STATUS_H' \
    '#define STATUS_H' \
    'int manifest_status(void);' \
    '#endif' > "$TMP/src/status.h"
printf '%s\n' \
    '#include "status.h"' \
    'int manifest_status(void) { return 7; }' > "$TMP/src/status.c"
printf '%s\n' \
    'output=manifest-app' \
    'source=src/main.c' \
    'source=src/status.c' \
    'expect_status=7' > "$TMP/vita.project"

PROJECT=$(VITA_DEV_CC=cc "$TOOL" project --machine --workdir "$TMP/project" \
    --manifest "$TMP/vita.project")
printf '%s\n' "$PROJECT"
printf '%s\n' "$PROJECT" | grep -F 'action=project' >/dev/null
printf '%s\n' "$PROJECT" | grep -F 'status=built' >/dev/null
printf '%s\n' "$PROJECT" | grep -F 'source_count=2' >/dev/null
printf '%s\n' "$PROJECT" | grep -F "output=$TMP/project/manifest-app" >/dev/null
[ -x "$TMP/project/manifest-app" ]

TEST=$(VITA_DEV_CC=cc "$TOOL" test --machine --workdir "$TMP/test" \
    --manifest "$TMP/vita.project")
printf '%s\n' "$TEST"
printf '%s\n' "$TEST" | grep -F 'action=test' >/dev/null
printf '%s\n' "$TEST" | grep -F 'status=pass' >/dev/null
printf '%s\n' "$TEST" | grep -F 'expected_status=7' >/dev/null
printf '%s\n' "$TEST" | grep -F 'run_status=7' >/dev/null
grep -F 'manifest-value:7' "$TMP/test/.vita-dev-test-output" >/dev/null

printf '%s\n' \
    'output=bad-app' \
    'source=src/main.c' \
    'source=src/status.c' \
    'expect_status=7' \
    'unknown=reject-me' > "$TMP/bad.project"
set +e
BAD=$(VITA_DEV_CC=cc "$TOOL" project --machine --workdir "$TMP/bad" \
    --manifest "$TMP/bad.project" 2>&1)
BAD_STATUS=$?
set -e
[ "$BAD_STATUS" -ne 0 ]
printf '%s\n' "$BAD" | grep -F 'status=manifest_unknown_key' >/dev/null

printf 'vita-dev manifest test passed\n'
