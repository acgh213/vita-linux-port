#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
TOOL="$ROOT/toolkit/bin/vita-fb"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' 0 HUP INT TERM

fixture() {
    rm -rf "$TMP/root"
    mkdir -p "$TMP/root/sys/class/graphics/fb0" \
        "$TMP/root/sys/class/vtconsole/vtcon1" "$TMP/root/dev"
    printf '1280,720\n' > "$TMP/root/sys/class/graphics/fb0/virtual_size"
    printf '32\n' > "$TMP/root/sys/class/graphics/fb0/bits_per_pixel"
    printf '5120\n' > "$TMP/root/sys/class/graphics/fb0/stride"
    printf 'U:1280x720p-0\n' > "$TMP/root/sys/class/graphics/fb0/modes"
    printf '1\n' > "$TMP/root/sys/class/vtconsole/vtcon1/bind"
    dd if=/dev/zero of="$TMP/root/dev/fb0" bs=4096 count=900 2>/dev/null
}

run_tool() {
    VITA_FB_ROOT="$TMP/root" "$TOOL" "$@"
}

chmod +x "$TOOL"
fixture

info=$(run_tool info --machine)
printf '%s\n' "$info"
printf '%s\n' "$info" | grep -F 'schema=1' >/dev/null
printf '%s\n' "$info" | grep -F 'geometry=1280,720' >/dev/null
printf '%s\n' "$info" | grep -F 'bits_per_pixel=32' >/dev/null
printf '%s\n' "$info" | grep -F 'stride_bytes=5120' >/dev/null
printf '%s\n' "$info" | grep -F 'frame_bytes=3686400' >/dev/null
printf '%s\n' "$info" | grep -F 'frame_blocks=900' >/dev/null
printf '%s\n' "$info" | grep -F 'fbcon_bound=1' >/dev/null

capture="$TMP/capture.raw"
run_tool capture "$capture"
[ "$(wc -c < "$capture" | tr -d ' ')" = 3686400 ]
cmp "$capture" "$TMP/root/dev/fb0"

# A short framebuffer must never be published as a valid capture.
rm -f "$capture"
dd if=/dev/zero of="$TMP/root/dev/fb0" bs=4096 count=1 2>/dev/null
if run_tool capture "$capture" >/dev/null 2>&1; then
    printf 'FAIL: short framebuffer capture was accepted\n' >&2
    exit 1
fi
[ ! -e "$capture" ]

# Restore a complete frame and preserve the original fbcon ownership.
fixture
save="$TMP/save.raw"
dd if=/dev/urandom of="$save" bs=4096 count=900 2>/dev/null
run_tool restore "$save"
cmp "$save" "$TMP/root/dev/fb0"
[ "$(tr -d '\n' < "$TMP/root/sys/class/vtconsole/vtcon1/bind")" = 1 ]

# A truncated restore must fail before touching fb0 and must rebind fbcon.
cp "$TMP/root/dev/fb0" "$TMP/before.raw"
dd if=/dev/zero of="$TMP/short.raw" bs=4096 count=1 2>/dev/null
if run_tool restore "$TMP/short.raw" >/dev/null 2>&1; then
    printf 'FAIL: short framebuffer restore was accepted\n' >&2
    exit 1
fi
cmp "$TMP/before.raw" "$TMP/root/dev/fb0"
[ "$(tr -d '\n' < "$TMP/root/sys/class/vtconsole/vtcon1/bind")" = 1 ]

# If fbcon started unbound, restore must leave it unbound.
printf '0\n' > "$TMP/root/sys/class/vtconsole/vtcon1/bind"
run_tool restore "$save"
[ "$(tr -d '\n' < "$TMP/root/sys/class/vtconsole/vtcon1/bind")" = 0 ]

printf 'vita-fb test passed\n'
