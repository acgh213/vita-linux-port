#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
BUILDER="$ROOT/tools/build-vita-toolkit-squashfs.sh"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' 0 HUP INT TERM

chmod +x "$BUILDER"
manifest=$("$BUILDER" --source "$ROOT/toolkit" --manifest-only)
printf '%s\n' "$manifest"
printf '%s\n' "$manifest" | grep -F 'VERSION ' >/dev/null
printf '%s\n' "$manifest" | grep -F 'bin/vita-diag ' >/dev/null
printf '%s\n' "$manifest" | grep -F 'bin/vita-netdiag ' >/dev/null
if printf '%s\n' "$manifest" | grep -E '\.ssh|wpa_supplicant|local/' >/dev/null; then
    printf 'FAIL: manifest contains forbidden local material\n' >&2
    exit 1
fi

dry_run=$("$BUILDER" --source "$ROOT/toolkit" --output "$TMP/vita-toolkit.squashfs" --dry-run)
printf '%s\n' "$dry_run" | grep -F -- '-comp zstd' >/dev/null
printf '%s\n' "$dry_run" | grep -F -- '-no-xattrs' >/dev/null
printf '%s\n' "$dry_run" | grep -F 'manifest:' >/dev/null

printf 'not an image\n' > "$TMP/existing.squashfs"
if "$BUILDER" --source "$ROOT/toolkit" --output "$TMP/existing.squashfs" --dry-run >/dev/null 2>&1; then
    printf 'FAIL: dry-run accepted an existing output without --force\n' >&2
    exit 1
fi

printf 'squashfs builder safe-path tests passed\n'
