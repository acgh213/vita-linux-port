#!/bin/sh
set -eu

ROOT=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
CONFIG="$ROOT/configs/vita_defconfig"
INPUT="$ROOT/board/vita/overlay/etc/bluetooth/input.conf"

grep -F 'BR2_PACKAGE_BLUEZ5_UTILS_PLUGINS_HID=y' "$CONFIG" >/dev/null
[ -f "$INPUT" ]
grep -Fx '[General]' "$INPUT" >/dev/null
grep -Fx 'UserspaceHID=false' "$INPUT" >/dev/null

printf 'bluetooth HID config test passed\n'
