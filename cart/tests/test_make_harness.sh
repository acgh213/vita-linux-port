#!/bin/sh
# Regression tests for the cart host capture Makefile contract.
set -eu

SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P)
CART_DIR=$(CDPATH='' cd -- "$SCRIPT_DIR/.." && pwd -P)
TMP_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/cart-make-harness.XXXXXX") || exit 1
REAL_MAKE=$(command -v make)
trap 'rm -rf "$TMP_ROOT"' EXIT HUP INT TERM

fail() {
    printf 'not ok - %s\n' "$1" >&2
    exit 1
}

copy_cart() {
    destination=$1
    cp -R "$CART_DIR" "$destination"
    rm -rf "$destination/build"
}

make_fake_capture() {
    path=$1
    cat >"$path" <<'EOF_CAPTURE'
#!/bin/sh
set -eu
scene=$1
output=$3
if [ "$scene" -eq 3 ]; then
    printf '%s\n' 'intentional capture failure' >&2
    exit 1
fi
printf 'P6\n320 180\n255\n' >"$output"
EOF_CAPTURE
    chmod +x "$path"
}

make_fake_cross_tools() {
    tool_dir=$1
    mkdir -p "$tool_dir"
    cat >"$tool_dir/cross-cc" <<'EOF_CROSS'
#!/bin/sh
set -eu
output=
while [ "$#" -gt 0 ]; do
    if [ "$1" = -o ]; then
        shift
        output=$1
    fi
    shift
done
[ -n "$output" ]
: >"$output"
EOF_CROSS
    cat >"$tool_dir/file" <<'EOF_FILE'
#!/bin/sh
printf '%s\n' 'ELF 32-bit LSB executable, ARM, EABI5, statically linked'
EOF_FILE
    cat >"$tool_dir/readelf" <<'EOF_READELF'
#!/bin/sh
case $1 in
    -h) printf '%s\n' '  Flags: 0x05004000, Version5 EABI, hard-float ABI' ;;
    -l) : ;;
    *) exit 2 ;;
esac
EOF_READELF
    chmod +x "$tool_dir/cross-cc" "$tool_dir/file" "$tool_dir/readelf"
}

# A fixture rejection must not be inferred from an unrelated child make failure.
FAKE_MAKE_DIR=$TMP_ROOT/fake-make
mkdir -p "$FAKE_MAKE_DIR"
cat >"$FAKE_MAKE_DIR/make" <<EOF_FAKE_MAKE
#!/bin/sh
exec "$REAL_MAKE" "\$@" CC=false
EOF_FAKE_MAKE
chmod +x "$FAKE_MAKE_DIR/make"
if PATH="$FAKE_MAKE_DIR:$PATH" "$CART_DIR/tests/test_fixture_immutability.sh" \
    >"$TMP_ROOT/immutability-output" 2>"$TMP_ROOT/immutability-error"; then
    cat "$TMP_ROOT/immutability-output" "$TMP_ROOT/immutability-error" >&2
    fail 'immutability harness accepted unrelated child make failure'
fi

# A failed scene must not publish a partial build/fixtures directory.
ATOMIC_CART=$TMP_ROOT/atomic-cart
copy_cart "$ATOMIC_CART"
mkdir -p "$ATOMIC_CART/build/fixtures"
printf '%s\n' old-generation >"$ATOMIC_CART/build/fixtures/sentinel"
make_fake_capture "$TMP_ROOT/failing-capture"
: >"$TMP_ROOT/failing-source"
touch "$TMP_ROOT/failing-capture"
if make -C "$ATOMIC_CART" host-capture CAPTURE_BIN="$TMP_ROOT/failing-capture" \
    CAPTURE_SRC="$TMP_ROOT/failing-source" \
    >"$TMP_ROOT/atomic-output" 2>"$TMP_ROOT/atomic-error"; then
    cat "$TMP_ROOT/atomic-output" "$TMP_ROOT/atomic-error" >&2
    fail 'host-capture accepted a failed scene'
fi
[ -f "$ATOMIC_CART/build/fixtures/sentinel" ] || \
    fail 'failed host-capture discarded the previous fixture generation'
[ ! -f "$ATOMIC_CART/build/fixtures/scene-0-frame-120.ppm" ] || \
    fail 'failed host-capture published a partial fixture generation'

# make test must execute the sanitizer-backed capture, not only compile it.
TEST_CART=$TMP_ROOT/test-cart
copy_cart "$TEST_CART"
FAKE_TOOLS=$TMP_ROOT/fake-tools
make_fake_cross_tools "$FAKE_TOOLS"
PATH="$FAKE_TOOLS:$PATH" CART_SKIP_IMMUTABILITY=1 \
    make -C "$TEST_CART" test CROSS_CC="$FAKE_TOOLS/cross-cc" CC=cc \
    >"$TMP_ROOT/test-output" 2>"$TMP_ROOT/test-error" || {
        cat "$TMP_ROOT/test-output" "$TMP_ROOT/test-error" >&2
        fail 'make test failed in the isolated host harness'
    }
[ -f "$TEST_CART/build/fixtures/manifest.txt" ] || \
    fail 'make test did not run host-capture'
scene=0
while [ "$scene" -lt 6 ]; do
    [ -f "$TEST_CART/build/fixtures/scene-$scene-frame-120.ppm" ] || \
        fail "make test did not capture scene $scene"
    scene=$((scene + 1))
done

# Host targets must work when the checkout path contains spaces.
SPACE_CART=$TMP_ROOT/cart\ with\ space
copy_cart "$SPACE_CART"
make -C "$SPACE_CART" host-sanitize >"$TMP_ROOT/space-output" 2>"$TMP_ROOT/space-error" || {
    cat "$TMP_ROOT/space-output" "$TMP_ROOT/space-error" >&2
    fail 'host-sanitize failed for a checkout path containing spaces'
}
[ -f "$SPACE_CART/build/pstv-demo-cart.host-sanitize" ] || \
    fail 'host-sanitize did not produce its binary in a space-containing path'

# Generated build artifacts must be ignored by the outer repository.
git -C "$CART_DIR/.." check-ignore -q --no-index cart/build/sentinel || \
    fail 'cart/build is not ignored by the outer repository'

printf 'ok - cart Makefile capture harness regressions\n'
