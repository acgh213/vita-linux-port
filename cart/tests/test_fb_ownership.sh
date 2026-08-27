#!/bin/sh
# Behavioral regression tests for framebuffer ownership wrappers.
set -eu

SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P) || exit 1
CART_DIR=$(CDPATH='' cd -- "$SCRIPT_DIR/.." && pwd -P) || exit 1
START_SOURCE=$CART_DIR/scripts/start-demo-cart.sh
STOP_SOURCE=$CART_DIR/scripts/stop-demo-cart.sh
TMP_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/cart-fb-ownership.XXXXXX") || exit 1

cleanup() {
    rm -rf "$TMP_ROOT"
}
trap cleanup 0 HUP INT TERM

fail() {
    printf 'not ok - %s\n' "$1" >&2
    exit 1
}

assert_file_equals() {
    file=$1
    expected=$2
    actual=$(cat "$file")
    [ "$actual" = "$expected" ] || fail "expected $file to contain $expected, got $actual"
}

assert_contains() {
    file=$1
    expected=$2
    if ! python3 - "$file" "$expected" <<'PY'
from pathlib import Path
import sys
if sys.argv[2] not in Path(sys.argv[1]).read_text():
    raise SystemExit(1)
PY
    then
        fail "expected $file to contain $expected"
    fi
}

rewrite_wrapper() {
    source=$1
    destination=$2
    fixture=$3
    python3 - "$source" "$destination" "$fixture" <<'PY'
from pathlib import Path
import re
import sys
source, destination, fixture = map(Path, sys.argv[1:])
text = source.read_text()
replacements = {
    "BIN": fixture / "bin/cart",
    "PID": fixture / "run/cart.pid",
    "LOG": fixture / "run/cart.log",
    "SAVE": fixture / "run/cart-before.raw",
    "BIND": fixture / "bind",
    "MARK": fixture / "run/cart-was-bound",
    "FB": fixture / "fb0",
}
for key, value in replacements.items():
    text, count = re.subn(rf"(?m)^{key}=.*$", f'{key}="{value}"', text)
    if count > 1:
        raise SystemExit(f"ambiguous {key} assignment in {source}")
destination.write_text(text)
destination.chmod(0o755)
PY
}

make_fixture() {
    name=$1
    FIXTURE=$TMP_ROOT/$name
    mkdir -p "$FIXTURE/bin" "$FIXTURE/run" "$FIXTURE/fake-bin"
    : > "$FIXTURE/fb0"
    printf '1\n' > "$FIXTURE/bind"
    : > "$FIXTURE/dd.log"
    : > "$FIXTURE/ssd.log"
    : > "$FIXTURE/bin/cart"
    chmod +x "$FIXTURE/bin/cart"
    rewrite_wrapper "$START_SOURCE" "$FIXTURE/start.sh" "$FIXTURE"
    rewrite_wrapper "$STOP_SOURCE" "$FIXTURE/stop.sh" "$FIXTURE"

    cat >"$FIXTURE/fake-bin/dd" <<'EOF_DD'
#!/bin/sh
set -eu
input=
output=
for arg in "$@"; do
    case $arg in
        if=*) input=${arg#if=} ;;
        of=*) output=${arg#of=} ;;
    esac
done
printf '%s -> %s\n' "$input" "$output" >> "$CART_TEST_DD_LOG"
case ${CART_TEST_DD_MODE:-valid} in
    valid)
        truncate -s 3686400 "$output"
        ;;
    truncated)
        printf x > "$output"
        ;;
    restore-fail)
        if [ "$input" = "$CART_TEST_SAVE" ]; then
            exit 1
        fi
        truncate -s 3686400 "$output"
        ;;
    *)
        printf 'unknown CART_TEST_DD_MODE\n' >&2
        exit 2
        ;;
esac
EOF_DD
    chmod +x "$FIXTURE/fake-bin/dd"

    cat >"$FIXTURE/fake-bin/start-stop-daemon" <<'EOF_SSD'
#!/bin/sh
set -eu
printf '%s\n' "$*" >> "$CART_TEST_SSD_LOG"
case " $* " in
    *' -S '*)
        [ "${CART_TEST_START_MODE:-success}" = fail ] && exit 1
        pidfile=
        previous=
        for arg in "$@"; do
            if [ "$previous" = p ]; then pidfile=$arg; fi
            [ "$arg" = -p ] && previous=p || previous=
        done
        : "${pidfile:?missing pid file}"
        sleep 30 &
        child=$!
        printf '%s\n' "$child" > "$pidfile"
        exit 0
        ;;
    *' -t '*)
        pidfile=
        previous=
        for arg in "$@"; do
            if [ "$previous" = p ]; then pidfile=$arg; fi
            [ "$arg" = -p ] && previous=p || previous=
        done
        [ -r "$pidfile" ] && kill -0 "$(cat "$pidfile")" 2>/dev/null
        ;;
    *' -K '*)
        exit 0
        ;;
    *)
        exit 0
        ;;
esac
EOF_SSD
    chmod +x "$FIXTURE/fake-bin/start-stop-daemon"

    cat >"$FIXTURE/fake-bin/sleep" <<'EOF_SLEEP'
#!/bin/sh
exit 0
EOF_SLEEP
    chmod +x "$FIXTURE/fake-bin/sleep"
}

run_wrapper() {
    wrapper=$1
    shift
    PATH="$FIXTURE/fake-bin:$PATH" \
    CART_TEST_DD_LOG="$FIXTURE/dd.log" \
    CART_TEST_SSD_LOG="$FIXTURE/ssd.log" \
    CART_TEST_SAVE="$FIXTURE/run/cart-before.raw" \
    "$wrapper" "$@"
}

# A short framebuffer capture must never be published, unbind fbcon, or launch.
make_fixture truncated-capture
if CART_TEST_DD_MODE=truncated run_wrapper "$FIXTURE/start.sh"; then
    fail 'start accepted a truncated framebuffer save'
fi
[ ! -e "$FIXTURE/run/cart-before.raw" ] || fail 'truncated save was published'
assert_file_equals "$FIXTURE/bind" 1
[ ! -s "$FIXTURE/ssd.log" ] || fail 'start launched after truncated framebuffer save'
printf 'ok - start rejects truncated framebuffer capture before launch\n'

# If launch fails after a valid save, the wrapper must restore the original frame
# before rebinding fbcon and must not leave a stale save behind.
make_fixture failed-launch
if CART_TEST_DD_MODE=valid CART_TEST_START_MODE=fail run_wrapper "$FIXTURE/start.sh"; then
    fail 'start unexpectedly succeeded when daemon launch failed'
fi
assert_contains "$FIXTURE/dd.log" "$FIXTURE/run/cart-before.raw -> $FIXTURE/fb0"
assert_file_equals "$FIXTURE/bind" 1
[ ! -e "$FIXTURE/run/cart-before.raw" ] || fail 'failed launch left published framebuffer save behind'
printf 'ok - failed launch restores framebuffer before rebinding fbcon\n'

# A stale PID must never result in a TERM request. It may be removed only after
# the wrapper has established that it is not the cart process.
make_fixture stale-pid
printf '999999\n' > "$FIXTURE/run/cart.pid"
truncate -s 3686400 "$FIXTURE/run/cart-before.raw"
if ! CART_TEST_DD_MODE=valid run_wrapper "$FIXTURE/stop.sh"; then
    fail 'stop failed while retiring a stale PID record'
fi
if python3 - "$FIXTURE/ssd.log" <<'PY'
from pathlib import Path
import sys
raise SystemExit(0 if ' -s TERM' in Path(sys.argv[1]).read_text() else 1)
PY
then
    fail 'stop issued TERM processing for stale PID'
fi
[ ! -e "$FIXTURE/run/cart.pid" ] || fail 'stale PID record was not retired'
printf 'ok - stop refuses to signal a stale PID\n'

# A truncated save is forensic evidence, not restore input. Refuse it, retain it,
# and rebind the console rather than writing partial bytes into the framebuffer.
make_fixture truncated-restore
printf 'x' > "$FIXTURE/run/cart-before.raw"
: > "$FIXTURE/run/cart-was-bound"
printf '0\n' > "$FIXTURE/bind"
if CART_TEST_DD_MODE=valid run_wrapper "$FIXTURE/stop.sh"; then
    fail 'stop accepted a truncated framebuffer save'
fi
if python3 - "$FIXTURE/dd.log" "$FIXTURE/run/cart-before.raw" <<'PY'
from pathlib import Path
import sys
raise SystemExit(0 if sys.argv[2] in Path(sys.argv[1]).read_text() else 1)
PY
then
    fail 'stop attempted to restore a truncated framebuffer save'
fi
[ -e "$FIXTURE/run/cart-before.raw" ] || fail 'stop discarded truncated framebuffer evidence'
assert_file_equals "$FIXTURE/bind" 1
printf 'ok - stop refuses truncated restore and safely rebinds fbcon\n'

printf 'all framebuffer ownership tests passed\n'
