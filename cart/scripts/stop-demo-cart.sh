#!/bin/sh
set -u

BIN=${CART_BIN:-/usr/local/bin/pstv-demo-cart}
PID=${CART_PID:-/run/pstv-demo-cart.pid}
SAVE=${CART_SAVE:-/run/pstv-demo-cart-before.raw}
BIND=${CART_BIND:-/sys/class/vtconsole/vtcon1/bind}
MARK=${CART_MARK:-/run/pstv-demo-cart-console-was-bound}
FB=${CART_FB:-/dev/fb0}
FRAME_BYTES=${CART_FB_BYTES:-3686400}
status=0

cart_pid_matches() {
    [ -r "$PID" ] || return 1
    start-stop-daemon -K -t -x "$BIN" -p "$PID" >/dev/null 2>&1
}

save_has_expected_size() {
    [ -r "$SAVE" ] || return 1
    size=$(wc -c < "$SAVE") || return 1
    [ "$size" = "$FRAME_BYTES" ]
}

if cart_pid_matches; then
    if ! start-stop-daemon -K -x "$BIN" -p "$PID" -s TERM; then
        printf '%s\n' 'PSTV Demo Cart: failed to request renderer shutdown' >&2
        status=1
    fi
    n=0
    while [ "$n" -lt 30 ] && cart_pid_matches; do
        sleep 0.1
        n=$((n + 1))
    done
    if cart_pid_matches; then
        printf '%s\n' 'PSTV Demo Cart: renderer did not stop; retaining PID record' >&2
        status=1
    else
        rm -f "$PID"
    fi
else
    # The pidfile is stale or names another executable; never signal it.
    rm -f "$PID"
fi

if [ -e "$SAVE" ]; then
    if save_has_expected_size; then
        if dd if="$SAVE" of="$FB" bs=4096 count=900 2>/dev/null; then
            rm -f "$SAVE"
        else
            printf '%s\n' 'PSTV Demo Cart: framebuffer restore failed; preserving save' >&2
            status=1
        fi
    else
        printf '%s\n' 'PSTV Demo Cart: refusing truncated framebuffer save; preserving save' >&2
        status=1
    fi
fi

if [ -e "$MARK" ]; then
    if [ -w "$BIND" ] && printf 1 > "$BIND"; then
        rm -f "$MARK"
    else
        printf '%s\n' 'PSTV Demo Cart: console restore failed; preserving marker' >&2
        status=1
    fi
fi

if [ "$status" -eq 0 ]; then
    echo "PSTV Demo Cart stopped; framebuffer console restored"
else
    printf '%s\n' 'PSTV Demo Cart stopped with recovery errors' >&2
fi
exit "$status"
