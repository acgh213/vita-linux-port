#!/bin/sh
set -eu

ROOT=$(mktemp -d)
trap 'rm -rf "$ROOT"' 0 HUP INT TERM
mkdir -p "$ROOT/sys/class/net/eth0" "$ROOT/sys/class/net/mlan0" "$ROOT/etc"
printf 'up\n' > "$ROOT/sys/class/net/eth0/operstate"
printf 'down\n' > "$ROOT/sys/class/net/mlan0/operstate"
printf 'nameserver 192.168.18.1\nnameserver 1.1.1.1\n' > "$ROOT/etc/resolv.conf"

out=$(VITA_NETDIAG_ROOT="$ROOT" \
    VITA_NETDIAG_ADDR='eth0:192.168.18.43/24' \
    VITA_NETDIAG_ROUTE='192.168.18.1:eth0' \
    ./toolkit/bin/vita-netdiag --machine)
printf '%s\n' "$out"
printf '%s\n' "$out" | grep -F 'schema=1' >/dev/null
printf '%s\n' "$out" | grep -F 'interfaces=eth0:up,mlan0:down' >/dev/null
printf '%s\n' "$out" | grep -F 'addresses=eth0:192.168.18.43/24' >/dev/null
printf '%s\n' "$out" | grep -F 'default_route=192.168.18.1:eth0' >/dev/null
printf '%s\n' "$out" | grep -F 'nameservers=192.168.18.1,1.1.1.1' >/dev/null
printf '%s\n' "$out" | grep -F 'probe_status=not_requested' >/dev/null
printf 'vita-netdiag test passed\n'
