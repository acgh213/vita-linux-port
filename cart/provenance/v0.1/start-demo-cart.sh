#!/bin/sh
set -eu

BIN=/usr/local/bin/pstv-demo-cart
PID=/run/pstv-demo-cart.pid
LOG=/run/pstv-demo-cart.log
SAVE=/run/pstv-demo-cart-before.raw
BIND=/sys/class/vtconsole/vtcon1/bind
MARK=/run/pstv-demo-cart-console-was-bound

if [ -r "$PID" ] && kill -0 "$(cat "$PID")" 2>/dev/null; then
    echo "PSTV Demo Cart is already running (pid $(cat "$PID"))"
    exit 0
fi

[ -x "$BIN" ] || { echo "missing executable: $BIN" >&2; exit 1; }
rm -f "$PID" "$LOG" "$MARK"
dd if=/dev/fb0 of="$SAVE" bs=4096 count=900 2>/dev/null

if [ -r "$BIND" ] && [ "$(cat "$BIND")" = 1 ]; then
    : > "$MARK"
    printf 0 > "$BIND"
fi

if ! start-stop-daemon -S -b -m -p "$PID" -x "$BIN" -O "$LOG"; then
    [ ! -e "$MARK" ] || printf 1 > "$BIND"
    rm -f "$MARK"
    exit 1
fi

sleep 1
if ! [ -r "$PID" ] || ! kill -0 "$(cat "$PID")" 2>/dev/null; then
    echo "PSTV Demo Cart failed to stay running" >&2
    [ ! -e "$MARK" ] || printf 1 > "$BIND"
    exit 1
fi

echo "PSTV Demo Cart running (pid $(cat "$PID")); log: $LOG"
