#!/bin/sh
set -eu

ROOT=$(CDPATH='' cd -- "$(dirname "$0")/../.." && pwd)
TOOL="$ROOT/toolkit/bin/vita-dev"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' 0 HUP INT TERM

[ -x "$TOOL" ] || { printf 'FAIL: missing executable %s\n' "$TOOL" >&2; exit 1; }

WORK="$TMP/project"
mkdir -p "$TMP/src"
printf '%s\n' \
    '#include "answer.h"' \
    '#include <stdio.h>' \
    'int main(void) {' \
    '    printf("project-answer:%d\\n", answer());' \
    '    return answer() == 42 ? 0 : 1;' \
    '}' > "$TMP/src/main.c"
printf '%s\n' \
    '#ifndef ANSWER_H' \
    '#define ANSWER_H' \
    'int answer(void);' \
    '#endif' > "$TMP/src/answer.h"
printf '%s\n' \
    '#include "answer.h"' \
    'int answer(void) { return 42; }' > "$TMP/src/answer.c"

project=$(VITA_DEV_CC=cc "$TOOL" project --machine --workdir "$WORK" \
    --output app "$TMP/src/main.c" "$TMP/src/answer.c")
printf '%s\n' "$project"
printf '%s\n' "$project" | grep -F 'schema=1' >/dev/null
printf '%s\n' "$project" | grep -F 'action=project' >/dev/null
printf '%s\n' "$project" | grep -F 'status=built' >/dev/null
printf '%s\n' "$project" | grep -F 'source_count=2' >/dev/null
printf '%s\n' "$project" | grep -F "output=$WORK/app" >/dev/null
[ -x "$WORK/app" ]
[ -f "$WORK/main.o" ]
[ -f "$WORK/answer.o" ]

run_output=$(VITA_DEV_CC=cc "$TOOL" run "$WORK/app")
printf '%s\n' "$run_output" | grep -F 'project-answer:42' >/dev/null

# Every generated object and the final binary must remain inside workdir.
if VITA_DEV_CC=cc "$TOOL" project --machine --workdir "$WORK" \
        --output "$TMP/outside" "$TMP/src/main.c" "$TMP/src/answer.c" > "$TMP/outside.out" 2>&1; then
    printf '%s\n' 'FAIL: project accepted output outside workdir' >&2
    exit 1
fi
grep -F 'status=output_outside_workdir' "$TMP/outside.out" >/dev/null
[ ! -e "$TMP/outside" ]

printf 'vita-dev project test passed\n'
