#!/bin/sh
# Verify deterministic six-scene host captures and imported provenance.
set -eu

SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P)
CART_DIR=$(CDPATH='' cd -- "$SCRIPT_DIR/.." && pwd -P)
FIXTURES=$CART_DIR/tests/fixtures
MANIFEST=$FIXTURES/manifest.txt
VERIFY_ONLY=0
COMPARE_TO=

while [ "$#" -gt 0 ]; do
    case $1 in
        --verify-only) VERIFY_ONLY=1; shift ;;
        --compare-to)
            [ "$#" -ge 2 ] || { printf 'usage: %s [--verify-only] [--compare-to DIR]\n' "$0" >&2; exit 2; }
            COMPARE_TO=$2
            shift 2
            ;;
        *) printf 'usage: %s [--verify-only] [--compare-to DIR]\n' "$0" >&2; exit 2 ;;
    esac
done

fail() {
    printf 'FAIL %s\n' "$1" >&2
    exit 1
}

assert_sha256() {
    file=$1
    expected=$2
    actual=$(sha256sum "$file" | awk '{print $1}')
    [ "$actual" = "$expected" ] || fail "$file sha256 ($actual != $expected)"
}

assert_sha256 "$CART_DIR/src/pstv-demo-cart.c" 22f4dc5f65a294f01fbb6f96fb99e624e7a7cd86dcd808c7da5dcc1a589240f1
assert_sha256 "$CART_DIR/scripts/start-demo-cart.sh" 5bf93b22524f16ae95c87c64f7a76443fd4423da64059850b06fcccbbf479a14
assert_sha256 "$CART_DIR/scripts/stop-demo-cart.sh" 0daae2bf622b4c07f1509a541f8729ace261dfcd7960605ee917fc935e7f28c0
assert_sha256 "$CART_DIR/README.md" 55f4acb385d4aa047178401d64bdc9a3421829406d40bfc58fbeb9a067d002ab

if [ "$VERIFY_ONLY" -eq 0 ]; then
    make -C "$CART_DIR" host-sanitize host-capture
fi

[ -f "$MANIFEST" ] || fail 'fixtures/manifest.txt missing'
awk -F= '
    /^[[:space:]]*$/ || /^[[:space:]]*#/ { next }
    NF != 2 { bad=1; next }
    { values[$1] = $2 }
    END {
        required[1]="manifest_version"; required[2]="logical_width";
        required[3]="logical_height"; required[4]="capture_width";
        required[5]="capture_height"; required[6]="scene_count";
        required[7]="frame";
        for (i=1; i<=7; i++) if (!(required[i] in values)) bad=1
        if (bad || values["manifest_version"] != 1 ||
            values["logical_width"] != 320 || values["logical_height"] != 180 ||
            values["capture_width"] != 320 || values["capture_height"] != 180 ||
            values["scene_count"] != 6 || values["frame"] != 120) exit 1
    }
' "$MANIFEST" || fail 'manifest metadata'

hashes=
scene=0
while [ "$scene" -lt 6 ]; do
    ppm=$FIXTURES/scene-$scene-frame-120.ppm
    [ -f "$ppm" ] || fail "$ppm missing"
    header=$(od -An -v -t x1 -N 2 "$ppm" | tr -d ' \n')
    [ "$header" = 5036 ] || fail "$ppm is not P6"
    dimensions=$(awk 'NR == 2 { print $1 " " $2 }' "$ppm")
    [ "$dimensions" = '320 180' ] || fail "$ppm dimensions ($dimensions)"
    maxval=$(awk 'NR == 3 { print $1 }' "$ppm")
    [ "$maxval" = 255 ] || fail "$ppm maxval ($maxval)"
    hash=$(sha256sum "$ppm" | awk '{print $1}')
    expected=$(awk -F= -v key="scene_${scene}_sha256" '$1 == key { print $2 }' "$MANIFEST")
    [ -n "$expected" ] || fail "manifest scene_${scene}_sha256 missing"
    [ "$hash" = "$expected" ] || fail "$ppm hash ($hash != $expected)"
    hashes="$hashes
$hash"
    scene=$((scene + 1))
done

unique=$(printf '%s\n' "$hashes" | awk 'NF { print }' | sort -u | awk 'END { print NR }')
[ "$unique" -eq 6 ] || fail "expected six distinct scene hashes, got $unique"

if [ -n "$COMPARE_TO" ]; then
    [ -d "$COMPARE_TO" ] || fail "generated fixture directory missing: $COMPARE_TO"
    scene=0
    while [ "$scene" -lt 6 ]; do
        generated=$COMPARE_TO/scene-$scene-frame-120.ppm
        [ -f "$generated" ] || fail "generated fixture missing: $generated"
        cmp -s "$FIXTURES/scene-$scene-frame-120.ppm" "$generated" || \
            fail "generated fixture $generated differs from committed fixture"
        scene=$((scene + 1))
    done
    cmp -s "$MANIFEST" "$COMPARE_TO/manifest.txt" || \
        fail 'generated fixture manifest differs from committed manifest'
    printf 'PASS generated six PPMs and manifest match committed fixtures\n'
fi

printf 'PASS imported source hashes\n'
printf 'PASS six deterministic 320x180 PPM fixtures (frame 120)\n'
printf 'PASS all six scene outputs are non-identical\n'
printf 'all legacy render tests passed\n'
