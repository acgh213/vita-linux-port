#!/bin/sh
set -u

PID=/run/pstv-demo-cart.pid
SAVE=/run/pstv-demo-cart-before.raw
BIND=/sys/class/vtconsole/vtcon1/bind
MARK=/run/pstv-demo-cart-console-was-bound

if [ -r "$PID" ]; then
    start-stop-daemon -K -o -p "$PID" -s TERM
    n=0
    while [ "$n" -lt 30 ] && kill -0 "$(cat "$PID")" 2>/dev/null; do
        sleep 0.1
        n=$((n + 1))
    done
fi
rm -f "$PID"

if [ -r "$SAVE" ]; then
    dd if="$SAVE" of=/dev/fb0 bs=4096 count=900 2>/dev/null
    rm -f "$SAVE"
fi
if [ -e "$MARK" ] && [ -w "$BIND" ]; then
    printf 1 > "$BIND"
    rm -f "$MARK"
fi

echo "PSTV Demo Cart stopped; framebuffer console restored"
