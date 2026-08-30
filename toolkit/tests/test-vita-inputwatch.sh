#!/bin/sh
set -eu

ROOT=$(CDPATH='' cd -- "$(dirname "$0")/../.." && pwd)
SOURCE="$ROOT/toolkit/share/vita-inputwatch.c"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' 0 HUP INT TERM

SYS="$TMP/sys"
DEV="$TMP/dev"
mkdir -p "$SYS/class/input/event0/device" "$SYS/class/input/event1/device" "$DEV/input"
printf 'PlayStation Vita Touchscreen (Syscon)\n' > "$SYS/class/input/event0/device/name"
printf 'vita_syscon_ts\n' > "$SYS/class/input/event0/device/phys"
printf 'PlayStation Vita Buttons (Syscon)\n' > "$SYS/class/input/event1/device/name"
printf 'vita_syscon_buttons\n' > "$SYS/class/input/event1/device/phys"

python3 - "$DEV/input/event1" <<'PY'
import struct
import sys
path = sys.argv[1]
records = [
    (1, 304, 1),   # key press
    (1, 304, 0),   # key release
    (1, 304, 2),   # key repeat
    (0, 0, 0),     # SYN_REPORT
]
with open(path, 'wb') as output:
    for event_type, code, value in records:
        output.write(struct.pack('@llHHi', 0, 0, event_type, code, value))
PY

cc -std=c11 -Wall -Wextra -Werror "$SOURCE" -o "$TMP/vita-inputwatch"
out=$(VITA_INPUTWATCH_SYSFS_ROOT="$SYS" VITA_INPUTWATCH_DEV_ROOT="$DEV" \
    "$TMP/vita-inputwatch" --machine --phys vita_syscon_buttons \
    --duration-ms 100 --max-events 4)
printf '%s\n' "$out"
printf '%s\n' "$out" | grep -F 'schema=1' >/dev/null
printf '%s\n' "$out" | grep -F 'input_read_only=1' >/dev/null
printf '%s\n' "$out" | grep -F 'status=complete' >/dev/null
printf '%s\n' "$out" | grep -F 'selected_event=event1' >/dev/null
printf '%s\n' "$out" | grep -F 'selected_device=/dev/input/event1' >/dev/null
printf '%s\n' "$out" | grep -F 'selected_phys=vita_syscon_buttons' >/dev/null
printf '%s\n' "$out" | grep -F 'events_seen=4' >/dev/null
printf '%s\n' "$out" | grep -F 'key_presses=1' >/dev/null
printf '%s\n' "$out" | grep -F 'key_releases=1' >/dev/null
printf '%s\n' "$out" | grep -F 'key_repeats=1' >/dev/null
printf '%s\n' "$out" | grep -F 'syn_reports=1' >/dev/null

printf 'vita-inputwatch test passed\n'
