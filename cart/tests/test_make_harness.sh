#!/bin/sh
# Regression tests for the cart host capture Makefile contract.
set -eu

SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P)
CART_DIR=$(CDPATH='' cd -- "$SCRIPT_DIR/.." && pwd -P)
TMP_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/cart-make-harness.XXXXXX") || exit 1
REAL_MAKE=$(command -v make)
cleanup() {
    status=$?
    trap - 0 HUP INT TERM
    rm -rf "$TMP_ROOT"
    exit "$status"
}
trap cleanup 0
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

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

make_fake_capture_without_failures() {
    path=$1
    cat >"$path" <<'EOF_CAPTURE_OK'
#!/bin/sh
set -eu
output=$3
printf 'P6\n320 180\n255\n' >"$output"
EOF_CAPTURE_OK
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

make_fake_final_mv() {
    path=$1
    cat >"$path" <<'EOF_FINAL_MV'
#!/bin/sh
set -eu
source=$1
destination=$2
if [ "$source" = -T ]; then
    source=$2
    destination=$3
fi
case "$source:$destination" in
    */.fixtures-link.*:*/fixtures) exit 1 ;;
esac
exec /bin/mv "$source" "$destination"
EOF_FINAL_MV
    chmod +x "$path"
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
if ! grep -a -F -q -- \
    'not ok - make test failed without reporting the expected fixture hash mismatch' \
    "$TMP_ROOT/immutability-output" "$TMP_ROOT/immutability-error"; then
    cat "$TMP_ROOT/immutability-output" "$TMP_ROOT/immutability-error" >&2
    fail 'immutability harness did not reject the unrelated child failure for the expected reason'
fi
if grep -a -F -q -- \
    'ok - make test rejects corrupted committed fixtures without regeneration' \
    "$TMP_ROOT/immutability-output" "$TMP_ROOT/immutability-error"; then
    cat "$TMP_ROOT/immutability-output" "$TMP_ROOT/immutability-error" >&2
    fail 'immutability harness reported success after an unrelated child failure'
fi

# A failed scene must not publish a partial build/fixtures directory.
ATOMIC_CART=$TMP_ROOT/atomic-cart
copy_cart "$ATOMIC_CART"
mkdir -p "$ATOMIC_CART/build/.fixtures-old"
printf '%s\n' old-generation >"$ATOMIC_CART/build/.fixtures-old/sentinel"
ln -s .fixtures-old "$ATOMIC_CART/build/fixtures"
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
[ -L "$ATOMIC_CART/build/fixtures" ] || \
    fail 'failed host-capture replaced the previous symlink generation'
[ ! -f "$ATOMIC_CART/build/fixtures/scene-0-frame-120.ppm" ] || \
    fail 'failed host-capture published a partial fixture generation'

# A legacy real directory must fail before capture rather than creating a
# publication gap or silently changing the supported representation.
REAL_DIR_CART=$TMP_ROOT/real-dir-cart
copy_cart "$REAL_DIR_CART"
mkdir -p "$REAL_DIR_CART/build/fixtures"
printf '%s\n' old-real-directory >"$REAL_DIR_CART/build/fixtures/sentinel"
if make -C "$REAL_DIR_CART" host-capture \
    >"$TMP_ROOT/real-dir-output" 2>"$TMP_ROOT/real-dir-error"; then
    cat "$TMP_ROOT/real-dir-output" "$TMP_ROOT/real-dir-error" >&2
    fail 'host-capture silently accepted a legacy real fixtures directory'
fi
if ! grep -a -F -q -- 'build/fixtures must be a symlink' \
    "$TMP_ROOT/real-dir-output" "$TMP_ROOT/real-dir-error"; then
    cat "$TMP_ROOT/real-dir-output" "$TMP_ROOT/real-dir-error" >&2
    fail 'legacy real fixtures directory failure lacked its explicit diagnostic'
fi
[ "$(cat "$REAL_DIR_CART/build/fixtures/sentinel")" = old-real-directory ] || \
    fail 'legacy real fixtures directory was mutated before rejection'
[ ! -e "$REAL_DIR_CART/build/.fixtures" ] || \
    fail 'legacy real fixtures rejection left a capture stage behind'

# The host binaries and generated wrapper must not be reused after a rebuild
# request, even when their mtimes make them look newer than their inputs.
REBUILD_CART=$TMP_ROOT/rebuild-cart
copy_cart "$REBUILD_CART"
REBUILD_TOOLS=$TMP_ROOT/rebuild-tools
mkdir -p "$REBUILD_TOOLS"
cat >"$REBUILD_TOOLS/cc" <<'EOF_CC'
#!/bin/sh
set -eu
printf '%s\n' compile >>"$CC_LOG"
exec cc "$@"
EOF_CC
chmod +x "$REBUILD_TOOLS/cc"
CC_LOG=$TMP_ROOT/cc.log
: >"$CC_LOG"
CC="$REBUILD_TOOLS/cc" CC_LOG="$CC_LOG" make -C "$REBUILD_CART" host-sanitize >/dev/null
CC="$REBUILD_TOOLS/cc" CC_LOG="$CC_LOG" make -C "$REBUILD_CART" host-sanitize >/dev/null
[ "$(wc -l <"$CC_LOG")" -ge 2 ] || \
    fail 'host-sanitize reused a stale compiler output'
grep -F -q "\$(HOST_BIN): \$(SRC) FORCE" "$CART_DIR/Makefile" || \
    fail 'host-sanitize lacks a deterministic rebuild dependency'
grep -F -q "\$(CAPTURE_SRC): \$(SRC) FORCE" "$CART_DIR/Makefile" || \
    fail 'generated capture wrapper lacks a deterministic rebuild dependency'
grep -F -q "\$(CAPTURE_BIN): \$(CAPTURE_SRC) FORCE" "$CART_DIR/Makefile" || \
    fail 'host capture binary lacks a deterministic rebuild dependency'

# The normal test path must compare every generated PPM and the generated
# manifest against the committed authoritative fixtures.
FAKE_TOOLS=$TMP_ROOT/fake-tools
make_fake_cross_tools "$FAKE_TOOLS"
MISMATCH_CART=$TMP_ROOT/mismatch-cart
copy_cart "$MISMATCH_CART"
make_fake_capture_without_failures "$TMP_ROOT/mismatch-capture"
: >"$TMP_ROOT/mismatch-source"
touch "$TMP_ROOT/mismatch-capture"
if CART_SKIP_IMMUTABILITY=1 make -C "$MISMATCH_CART" test \
    CAPTURE_BIN="$TMP_ROOT/mismatch-capture" \
    CAPTURE_SRC="$TMP_ROOT/mismatch-source" \
    PATH="$FAKE_TOOLS:$PATH" CROSS_CC="$FAKE_TOOLS/cross-cc" \
    >"$TMP_ROOT/mismatch-output" 2>"$TMP_ROOT/mismatch-error"; then
    cat "$TMP_ROOT/mismatch-output" "$TMP_ROOT/mismatch-error" >&2
    fail 'make test accepted generated fixtures that differ from committed fixtures'
fi
if ! grep -a -F -q -- 'generated fixture' "$TMP_ROOT/mismatch-output" "$TMP_ROOT/mismatch-error"; then
    cat "$TMP_ROOT/mismatch-output" "$TMP_ROOT/mismatch-error" >&2
    fail 'make test did not report the generated fixture comparison failure'
fi

# Publication must use a sibling temporary symlink and an atomic final rename.
grep -F -q "mktemp \"\$(BUILD_DIR)/.fixtures-link.XXXXXX\"" "$CART_DIR/Makefile" || \
    fail 'fixture publication does not allocate a temporary symlink path'
grep -F -q 'ln -s' "$CART_DIR/Makefile" || \
    fail 'fixture publication does not create a symlink generation'
grep -F -q 'mv -T' "$CART_DIR/Makefile" || \
    fail 'fixture publication does not replace the symlink itself'
grep -F -q 'CART_IMMUTABILITY_GUARD' "$CART_DIR/Makefile" || \
    fail 'fixture immutability recursion lacks an explicit internal guard'

# Shell traps must use POSIX trap 0 and exit through the cleanup handler on
# signals; EXIT is not portable and signal traps must not continue execution.
if grep -E -q 'trap .*EXIT HUP' "$CART_DIR/tests/test_fixture_immutability.sh"; then
    fail "$CART_DIR/tests/test_fixture_immutability.sh uses non-portable EXIT trap"
fi
if grep -E -q 'trap cleanup 0 HUP INT TERM' "$CART_DIR/Makefile"; then
    fail 'Makefile signal traps do not terminate after cleanup'
fi

# make test must execute the sanitizer-backed capture, not only compile it.
TEST_CART=$TMP_ROOT/test-cart
copy_cart "$TEST_CART"
FAKE_TOOLS=$TMP_ROOT/fake-tools
make_fake_cross_tools "$FAKE_TOOLS"
PATH="$FAKE_TOOLS:$PATH" CART_IMMUTABILITY_GUARD=child \
    make -C "$TEST_CART" test CROSS_CC="$FAKE_TOOLS/cross-cc" CC=cc \
    >"$TMP_ROOT/test-output" 2>"$TMP_ROOT/test-error" || {
        cat "$TMP_ROOT/test-output" "$TMP_ROOT/test-error" >&2
        fail 'make test failed in the isolated host harness'
    }
[ -f "$TEST_CART/build/fixtures/manifest.txt" ] || \
    fail 'make test did not run host-capture'
[ -L "$TEST_CART/build/fixtures" ] || \
    fail 'make test did not publish fixtures through a symlink'
scene=0
while [ "$scene" -lt 6 ]; do
    [ -f "$TEST_CART/build/fixtures/scene-$scene-frame-120.ppm" ] || \
        fail "make test did not capture scene $scene"
    scene=$((scene + 1))
done

# Generated captures must have real PPM header newlines and match the
# authoritative scene-0 fixture byte-for-byte.
SCENE_0="$TEST_CART/build/fixtures/scene-0-frame-120.ppm"
header=$(od -An -v -t x1 -N 15 "$SCENE_0" | tr -d ' \n')
[ "$header" = 50360a333230203138300a3235350a ] || \
    fail "generated scene-0 PPM header bytes ($header)"
expected_hash=$(sha256sum "$TEST_CART/tests/fixtures/scene-0-frame-120.ppm" | awk '{print $1}')
actual_hash=$(sha256sum "$SCENE_0" | awk '{print $1}')
[ "$actual_hash" = "$expected_hash" ] || \
    fail "generated scene-0 PPM hash ($actual_hash != $expected_hash)"
cmp -s "$TEST_CART/tests/fixtures/scene-0-frame-120.ppm" "$SCENE_0" || \
    fail 'generated scene-0 PPM differs from the authoritative fixture'

# A failed final publication rename must return nonzero and preserve the old
# generation instead of being reported as a successful capture.
RENAME_CART=$TMP_ROOT/rename-cart
copy_cart "$RENAME_CART"
mkdir -p "$RENAME_CART/build/.fixtures-old"
printf '%s\n' old-generation >"$RENAME_CART/build/.fixtures-old/sentinel"
ln -s .fixtures-old "$RENAME_CART/build/fixtures"
RENAME_TOOLS=$TMP_ROOT/rename-tools
mkdir -p "$RENAME_TOOLS"
make_fake_final_mv "$RENAME_TOOLS/mv"
if PATH="$RENAME_TOOLS:$PATH" make -C "$RENAME_CART" host-capture \
    >"$TMP_ROOT/rename-output" 2>"$TMP_ROOT/rename-error"; then
    cat "$TMP_ROOT/rename-output" "$TMP_ROOT/rename-error" >&2
    fail 'host-capture reported success after final publication rename failed'
fi
[ "$(cat "$RENAME_CART/build/fixtures/sentinel")" = old-generation ] || \
    fail 'final publication rename failure did not preserve the old fixture generation'
[ ! -e "$RENAME_CART/build/fixtures/scene-0-frame-120.ppm" ] || \
    fail 'final publication rename failure published a new fixture generation'

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
