#!/bin/sh
set -eu

ROOT=$(CDPATH='' cd -- "$(dirname "$0")/../.." && pwd)
TOOL="$ROOT/toolkit/bin/vita-usbinfo"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' 0 HUP INT TERM

[ -x "$TOOL" ] || { printf 'FAIL: missing executable %s\n' "$TOOL" >&2; exit 1; }

SYS="$TMP/sys"
DEV="$TMP/dev"
MOUNTS="$TMP/mounts"
mkdir -p "$SYS/bus/usb/devices/1-1" \
    "$SYS/bus/usb/devices/usb1" \
    "$SYS/bus/usb/drivers/usb-storage" \
    "$SYS/class/block/sdb" "$SYS/class/block/sdb1" "$SYS/bus/usb/devices/1-1/power" \
    "$SYS/bus/usb/devices/usb1/power" "$DEV"

# A real USB storage device below the EHCI root hub.
printf '1\n' > "$SYS/bus/usb/devices/1-1/busnum"
printf '2\n' > "$SYS/bus/usb/devices/1-1/devnum"
printf '0781\n' > "$SYS/bus/usb/devices/1-1/idVendor"
printf '5581\n' > "$SYS/bus/usb/devices/1-1/idProduct"
printf '08\n' > "$SYS/bus/usb/devices/1-1/bDeviceClass"
printf '480\n' > "$SYS/bus/usb/devices/1-1/speed"
printf 'SanDisk\n' > "$SYS/bus/usb/devices/1-1/manufacturer"
printf 'Cruzer\n' > "$SYS/bus/usb/devices/1-1/product"
printf 'active\n' > "$SYS/bus/usb/devices/1-1/power/runtime_status"
ln -s "$SYS/bus/usb/drivers/usb-storage" "$SYS/bus/usb/devices/1-1/driver"
ln -s "$SYS/bus/usb/devices/1-1" "$SYS/class/block/sdb/device"
ln -s "$SYS/bus/usb/devices/1-1" "$SYS/class/block/sdb1/device"
: > "$DEV/sdb"
: > "$DEV/sdb1"

# A root hub/controller record, plus an interface entry the tool must skip.
printf '1\n' > "$SYS/bus/usb/devices/usb1/busnum"
printf '1\n' > "$SYS/bus/usb/devices/usb1/devnum"
printf '1d6b\n' > "$SYS/bus/usb/devices/usb1/idVendor"
printf '0002\n' > "$SYS/bus/usb/devices/usb1/idProduct"
printf '09\n' > "$SYS/bus/usb/devices/usb1/bDeviceClass"
printf '480\n' > "$SYS/bus/usb/devices/usb1/speed"
printf 'Linux\n' > "$SYS/bus/usb/devices/usb1/manufacturer"
printf 'EHCI Root Hub\n' > "$SYS/bus/usb/devices/usb1/product"
printf 'active\n' > "$SYS/bus/usb/devices/usb1/power/runtime_status"
mkdir -p "$SYS/bus/usb/devices/1-1:1.0"
printf '08\n' > "$SYS/bus/usb/devices/1-1:1.0/bInterfaceClass"
printf '/dev/sdb1 /mnt/vita-storage exfat ro,relatime 0 0\n' > "$MOUNTS"

before=$(sha256sum "$MOUNTS" "$SYS/bus/usb/devices/1-1/idVendor")
output=$(VITA_USB_SYSFS_ROOT="$SYS" VITA_USB_DEV_ROOT="$DEV" \
    VITA_USB_PROC_MOUNTS="$MOUNTS" "$TOOL" --machine)
printf '%s\n' "$output"
printf '%s\n' "$output" | grep -F 'schema=1' >/dev/null
printf '%s\n' "$output" | grep -F 'usb_read_only=1' >/dev/null
printf '%s\n' "$output" | grep -F 'usb_count=2' >/dev/null
printf '%s\n' "$output" | grep -F 'usb_1_name=1-1' >/dev/null
printf '%s\n' "$output" | grep -F 'usb_1_parent=usb1' >/dev/null
printf '%s\n' "$output" | grep -F 'usb_1_vid=0781' >/dev/null
printf '%s\n' "$output" | grep -F 'usb_1_pid=5581' >/dev/null
printf '%s\n' "$output" | grep -F 'usb_1_class=08' >/dev/null
printf '%s\n' "$output" | grep -F 'usb_1_speed=480' >/dev/null
printf '%s\n' "$output" | grep -F 'usb_1_driver=usb-storage' >/dev/null
printf '%s\n' "$output" | grep -F 'usb_1_runtime_status=active' >/dev/null
printf '%s\n' "$output" | grep -F 'usb_1_block=sdb,sdb1' >/dev/null
printf '%s\n' "$output" | grep -F 'usb_1_mounts=/mnt/vita-storage' >/dev/null
if printf '%s\n' "$output" | grep -F '1-1:1.0' >/dev/null; then
    printf '%s\n' 'FAIL: USB interface leaked into device inventory' >&2
    exit 1
fi

after=$(sha256sum "$MOUNTS" "$SYS/bus/usb/devices/1-1/idVendor")
[ "$before" = "$after" ]
printf 'vita-usbinfo test passed\n'
