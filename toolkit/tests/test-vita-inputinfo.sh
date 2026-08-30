#!/bin/sh
set -eu

ROOT=$(CDPATH='' cd -- "$(dirname "$0")/../.." && pwd)
TOOL="$ROOT/toolkit/bin/vita-inputinfo"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' 0 HUP INT TERM

[ -x "$TOOL" ] || { printf 'FAIL: missing executable %s\n' "$TOOL" >&2; exit 1; }

SYS="$TMP/sys"
mkdir -p "$SYS/class/input/event0/device/capabilities" \
    "$SYS/class/input/event1/device/capabilities"
printf 'PlayStation Vita Touchscreen (Syscon)\n' > "$SYS/class/input/event0/device/name"
printf 'vita_syscon_ts\n' > "$SYS/class/input/event0/device/phys"
printf 'b\n' > "$SYS/class/input/event0/device/capabilities/ev"
printf '400 0 0 0 0 0 0 0 0 0 0\n' > "$SYS/class/input/event0/device/capabilities/key"
printf '6608000 1000003\n' > "$SYS/class/input/event0/device/capabilities/abs"
printf 'PlayStation Vita Buttons (Syscon)\n' > "$SYS/class/input/event1/device/name"
printf 'vita_syscon_buttons\n' > "$SYS/class/input/event1/device/phys"
printf 'b\n' > "$SYS/class/input/event1/device/capabilities/ev"
printf 'f 0 0 0 0 0 0 0 1cdb0000 0 0 0 0 0 1c0000 0 0\n' > "$SYS/class/input/event1/device/capabilities/key"
printf '1b\n' > "$SYS/class/input/event1/device/capabilities/abs"

before=$(sha256sum \
    "$SYS/class/input/event0/device/name" \
    "$SYS/class/input/event1/device/phys")
output=$(VITA_INPUT_SYSFS_ROOT="$SYS" "$TOOL" --machine)
printf '%s\n' "$output"
printf '%s\n' "$output" | grep -F 'schema=1' >/dev/null
printf '%s\n' "$output" | grep -F 'input_read_only=1' >/dev/null
printf '%s\n' "$output" | grep -F 'input_count=2' >/dev/null
printf '%s\n' "$output" | grep -F 'input_1_event=event0' >/dev/null
printf '%s\n' "$output" | grep -F 'input_1_device=/dev/input/event0' >/dev/null
printf '%s\n' "$output" | grep -F 'input_1_name=PlayStation Vita Touchscreen (Syscon)' >/dev/null
printf '%s\n' "$output" | grep -F 'input_1_phys=vita_syscon_ts' >/dev/null
printf '%s\n' "$output" | grep -F 'input_1_ev=b' >/dev/null
printf '%s\n' "$output" | grep -F 'input_1_key=400 0 0 0 0 0 0 0 0 0 0' >/dev/null
printf '%s\n' "$output" | grep -F 'input_1_abs=6608000 1000003' >/dev/null
printf '%s\n' "$output" | grep -F 'input_2_event=event1' >/dev/null
printf '%s\n' "$output" | grep -F 'input_2_device=/dev/input/event1' >/dev/null
printf '%s\n' "$output" | grep -F 'input_2_name=PlayStation Vita Buttons (Syscon)' >/dev/null
printf '%s\n' "$output" | grep -F 'input_2_phys=vita_syscon_buttons' >/dev/null
printf '%s\n' "$output" | grep -F 'input_2_key=f 0 0 0 0 0 0 0 1cdb0000 0 0 0 0 0 1c0000 0 0' >/dev/null
printf '%s\n' "$output" | grep -F 'input_2_abs=1b' >/dev/null
after=$(sha256sum \
    "$SYS/class/input/event0/device/name" \
    "$SYS/class/input/event1/device/phys")
[ "$before" = "$after" ]
printf 'vita-inputinfo test passed\n'
