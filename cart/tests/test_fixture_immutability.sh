#!/bin/sh
# Prove make test rejects corrupted committed fixtures without regenerating them.
set -eu

SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P)
CART_DIR=$(CDPATH='' cd -- "$SCRIPT_DIR/.." && pwd -P)
TMP_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/cart-fixture-immutability.XXXXXX") || exit 1

cleanup() {
    rm -rf "$TMP_ROOT"
}
trap cleanup EXIT HUP INT TERM

fail() {
    printf 'not ok - %s\n' "$1" >&2
    exit 1
}

COPY=$TMP_ROOT/cart
cp -R "$CART_DIR" "$COPY"
CORRUPTED=$COPY/tests/fixtures/scene-0-frame-120.ppm
MARKER='corrupted committed fixture must survive make test'
printf '%s\n' "$MARKER" >>"$CORRUPTED"

if CART_SKIP_IMMUTABILITY=1 make -C "$COPY" test >"$TMP_ROOT/output" 2>"$TMP_ROOT/error"; then
    cat "$TMP_ROOT/output" "$TMP_ROOT/error" >&2
    fail 'make test unexpectedly accepted a corrupted committed fixture'
fi

if ! grep -a -F -q -- "$MARKER" "$CORRUPTED"; then
    cat "$TMP_ROOT/output" "$TMP_ROOT/error" >&2
    fail 'make test regenerated the corrupted committed fixture'
fi

printf 'ok - make test rejects corrupted committed fixtures without regeneration\n'
