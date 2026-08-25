#!/bin/sh
set -eu

ROOT=$(mktemp -d)
trap 'rm -rf "$ROOT"' 0 HUP INT TERM
mkdir -p "$ROOT/proc" "$ROOT/sys/devices/system/cpu" \
    "$ROOT/sys/class/graphics/fb0" "$ROOT/sys/class/net/eth0" \
    "$ROOT/sys/class/net/lo" "$ROOT/dev/vita"
printf '123.45 0.00\n' > "$ROOT/proc/uptime"
printf 'MemTotal:       65536 kB\nMemAvailable:   32768 kB\n' > "$ROOT/proc/meminfo"
printf '0-3\n' > "$ROOT/sys/devices/system/cpu/online"
printf '1280,720\n' > "$ROOT/sys/class/graphics/fb0/virtual_size"
printf '32\n' > "$ROOT/sys/class/graphics/fb0/bits_per_pixel"
printf 'up\n' > "$ROOT/sys/class/net/eth0/operstate"
printf 'unknown\n' > "$ROOT/sys/class/net/lo/operstate"
ln -s fake-os0 "$ROOT/dev/vita/os0"
ln -s fake-ur0 "$ROOT/dev/vita/ur0"

out=$(VITA_DIAG_ROOT="$ROOT" VITA_DIAG_UNAME_R=6.12-test \
    VITA_DIAG_UNAME_M=armv7l ./toolkit/bin/vita-diag --machine)
printf '%s\n' "$out"
printf '%s\n' "$out" | grep -F 'schema=1' >/dev/null
printf '%s\n' "$out" | grep -F 'kernel=6.12-test' >/dev/null
printf '%s\n' "$out" | grep -F 'cpu_online=0-3' >/dev/null
printf '%s\n' "$out" | grep -F 'mem_total_kb=65536' >/dev/null
printf '%s\n' "$out" | grep -F 'framebuffer_geometry=1280,720' >/dev/null
printf '%s\n' "$out" | grep -F 'interfaces=eth0:up,lo:unknown' >/dev/null
printf '%s\n' "$out" | grep -F 'vita_links=os0:fake-os0,ur0:fake-ur0' >/dev/null
printf 'vita-diag test passed\n'
