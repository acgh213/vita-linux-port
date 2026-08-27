#!/bin/sh
set -eu

BIN=${CART_BIN:-/usr/local/bin/pstv-demo-cart}
PID=${CART_PID:-/run/pstv-demo-cart.pid}
LOG=${CART_LOG:-/run/pstv-demo-cart.log}
SAVE=${CART_SAVE:-/run/pstv-demo-cart-before.raw}
BIND=${CART_BIND:-/sys/class/vtconsole/vtcon1/bind}
MARK=${CART_MARK:-/run/pstv-demo-cart-console-was-bound}
FB=${CART_FB:-/dev/fb0}
FRAME_BYTES=${CART_FB_BYTES:-3686400}
SAVE_TMP=${SAVE}.tmp.$$
started=0

cart_pid_matches() {
    [ -r "$PID" ] || return 1
    start-stop-daemon -K -t -x "$BIN" -p "$PID" >/dev/null 2>&1
}

save_has_expected_size() {
    [ -r "$SAVE" ] || return 1
    size=$(wc -c < "$SAVE") || return 1
    [ "$size" = "$FRAME_BYTES" ]
}

restore_saved_frame() {
    save_has_expected_size || return 1
    dd if="$SAVE" of="$FB" bs=4096 count=900 2>/dev/null || return 1
    rm -f "$SAVE"
}

restore_console() {
    [ -e "$MARK" ] || return 0
    [ -w "$BIND" ] || return 1
    printf 1 > "$BIND" || return 1
    rm -f "$MARK"
}

cleanup() {
    status=$?
    trap - 0 HUP INT TERM
    rm -f "$SAVE_TMP"
    if [ "$started" -eq 0 ]; then
        if [ -e "$SAVE" ] && ! restore_saved_frame; then
            printf '%s\n' 'PSTV Demo Cart: framebuffer restore failed; preserving save' >&2
            status=1
        fi
        if ! restore_console; then
            printf '%s\n' 'PSTV Demo Cart: console restore failed' >&2
            status=1
        fi
    fi
    exit "$status"
}
trap cleanup 0
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

if cart_pid_matches; then
    echo "PSTV Demo Cart is already running (pid $(cat "$PID"))"
    started=1
    exit 0
fi

[ -x "$BIN" ] || { echo "missing executable: $BIN" >&2; exit 1; }
rm -f "$PID" "$LOG" "$MARK" "$SAVE_TMP"

if ! dd if="$FB" of="$SAVE_TMP" bs=4096 count=900 2>/dev/null; then
    printf '%s\n' 'PSTV Demo Cart: framebuffer save failed' >&2
    exit 1
fi
size=$(wc -c < "$SAVE_TMP") || exit 1
if [ "$size" != "$FRAME_BYTES" ]; then
    printf 'PSTV Demo Cart: framebuffer save has %s bytes, expected %s\n' "$size" "$FRAME_BYTES" >&2
    exit 1
fi
mv "$SAVE_TMP" "$SAVE"

if [ -r "$BIND" ] && [ "$(cat "$BIND")" = 1 ]; then
    : > "$MARK"
    printf 0 > "$BIND"
fi

if ! start-stop-daemon -S -b -m -p "$PID" -x "$BIN" -O "$LOG"; then
    printf '%s\n' 'PSTV Demo Cart: launch failed' >&2
    exit 1
fi

sleep 1
if ! cart_pid_matches; then
    rm -f "$PID"
    printf '%s\n' 'PSTV Demo Cart failed to stay running' >&2
    exit 1
fi

started=1
echo "PSTV Demo Cart running (pid $(cat "$PID")); log: $LOG"
