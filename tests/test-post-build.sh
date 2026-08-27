#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
POST_BUILD="$ROOT/buildroot-vita/board/vita/post_build.sh"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' 0 HUP INT TERM

mkdir -p "$TMP/target"
if "$POST_BUILD" "$TMP/target"; then
    test -d "$TMP/target/mnt/os0"
    test -d "$TMP/target/var/empty"
else
    printf 'FAIL: post-build rejected a target without optional SSH files\n' >&2
    exit 1
fi

printf 'post-build optional-file test passed\n'
