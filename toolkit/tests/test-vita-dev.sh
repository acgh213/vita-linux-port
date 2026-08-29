#!/bin/sh
set -eu

ROOT=$(CDPATH='' cd -- "$(dirname "$0")/../.." && pwd)
TOOL="$ROOT/toolkit/bin/vita-dev"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' 0 HUP INT TERM

[ -x "$TOOL" ] || { printf 'FAIL: missing executable %s\n' "$TOOL" >&2; exit 1; }

SOURCE="$TMP/hello.c"
WORK="$TMP/work"
printf '%s\n' \
    '#include <stdio.h>' \
    '#include <string.h>' \
    'int main(int argc, char **argv) {' \
    '    printf("hello:%s\\n", argc > 1 ? argv[1] : "none");' \
    '    return argc > 1 && strcmp(argv[1], "world") == 0 ? 7 : 9;' \
    '}' > "$SOURCE"

info=$(VITA_DEV_CC=cc "$TOOL" info --machine)
printf '%s\n' "$info"
printf '%s\n' "$info" | grep -F 'schema=1' >/dev/null
printf '%s\n' "$info" | grep -F 'action=info' >/dev/null
printf '%s\n' "$info" | grep -F 'dynamic_linking=1' >/dev/null
printf '%s\n' "$info" | grep -F 'static_linking=unsupported' >/dev/null

build=$(VITA_DEV_CC=cc "$TOOL" build --machine --workdir "$WORK" "$SOURCE")
printf '%s\n' "$build"
printf '%s\n' "$build" | grep -F 'status=built' >/dev/null
printf '%s\n' "$build" | grep -F "source=$SOURCE" >/dev/null
[ -x "$WORK/hello" ]

set +e
VITA_DEV_CC=cc "$TOOL" run "$WORK/hello" world > "$TMP/run.out"
run_status=$?
set -e
[ "$run_status" = 7 ]
grep -F 'hello:world' "$TMP/run.out" >/dev/null

# Output paths outside the declared workdir must fail before invoking the compiler.
if VITA_DEV_CC=cc "$TOOL" build --machine --workdir "$WORK" \
        --output "$TMP/outside" "$SOURCE" > "$TMP/outside.out" 2>&1; then
    printf '%s\n' 'FAIL: build accepted output outside workdir' >&2
    exit 1
fi
grep -F 'status=output_outside_workdir' "$TMP/outside.out" >/dev/null
[ ! -e "$TMP/outside" ]

printf 'vita-dev test passed\n'
