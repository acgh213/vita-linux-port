#!/bin/sh
# Build an ARM-native TinyCC + matched glibc development sysroot payload.
#
# The output tree is rooted exactly as it will appear when the SquashFS is
# mounted at /opt/vita-toolkit. The source tree is copied before configuration
# because TinyCC's build is in-tree; the pinned checkout is never modified.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
TCC_SOURCE=
SYSROOT=
OUTPUT=dist/vita-native-toolchain
EXPECTED_REVISION=
CROSS_PREFIX=${CROSS_COMPILE:-arm-linux-gnueabihf-}
JOBS=${JOBS:-4}
FORCE=0

usage() {
    printf '%s\n' \
        'usage: tools/build-vita-native-toolchain.sh [options]' \
        '  --tcc-source DIR         pinned TinyCC git checkout (required)' \
        '  --sysroot DIR            ARM hard-float sysroot with include/ + lib/' \
        '  --expected-revision SHA  require this exact TinyCC commit' \
        '  --output DIR             output tree (default: dist/vita-native-toolchain)' \
        '  --cross-prefix PREFIX    tool prefix (default: arm-linux-gnueabihf-)' \
        '  --jobs N                 parallel build jobs (default: 4)' \
        '  --force                  atomically replace an existing output tree'
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --tcc-source) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; TCC_SOURCE=$2; shift 2 ;;
        --sysroot) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; SYSROOT=$2; shift 2 ;;
        --expected-revision) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; EXPECTED_REVISION=$2; shift 2 ;;
        --output) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; OUTPUT=$2; shift 2 ;;
        --cross-prefix) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; CROSS_PREFIX=$2; shift 2 ;;
        --jobs) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; JOBS=$2; shift 2 ;;
        --force) FORCE=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) printf 'unknown option: %s\n' "$1" >&2; usage >&2; exit 2 ;;
    esac
done

[ -n "$TCC_SOURCE" ] || { printf '%s\n' 'missing --tcc-source' >&2; exit 2; }
[ -n "$SYSROOT" ] || { printf '%s\n' 'missing --sysroot' >&2; exit 2; }
[ -d "$TCC_SOURCE/.git" ] || { printf 'TCC source is not a git checkout: %s\n' "$TCC_SOURCE" >&2; exit 1; }
[ -x "$TCC_SOURCE/configure" ] || { printf 'missing TCC configure: %s/configure\n' "$TCC_SOURCE" >&2; exit 1; }
[ -f "$TCC_SOURCE/Makefile" ] || { printf 'missing TCC Makefile: %s/Makefile\n' "$TCC_SOURCE" >&2; exit 1; }
[ -f "$TCC_SOURCE/lib/Makefile" ] || { printf 'missing TCC runtime Makefile: %s/lib/Makefile\n' "$TCC_SOURCE" >&2; exit 1; }
[ -d "$SYSROOT/include" ] || { printf 'missing sysroot headers: %s/include\n' "$SYSROOT" >&2; exit 1; }
[ -d "$SYSROOT/lib" ] || { printf 'missing sysroot libraries: %s/lib\n' "$SYSROOT" >&2; exit 1; }
for required in include/stdio.h lib/crt1.o lib/libc.so lib/ld-linux-armhf.so.3; do
    [ -e "$SYSROOT/$required" ] || { printf 'missing sysroot file: %s/%s\n' "$SYSROOT" "$required" >&2; exit 1; }
done

if [ -z "$EXPECTED_REVISION" ] && [ -f "$ROOT/toolkit/toolchain/TCC_REVISION" ]; then
    EXPECTED_REVISION=$(sed -n '1p' "$ROOT/toolkit/toolchain/TCC_REVISION")
fi
[ -n "$EXPECTED_REVISION" ] || { printf '%s\n' 'missing --expected-revision and toolkit/toolchain/TCC_REVISION' >&2; exit 2; }
ACTUAL_REVISION=$(git -C "$TCC_SOURCE" rev-parse HEAD)
if [ "$ACTUAL_REVISION" != "$EXPECTED_REVISION" ]; then
    printf 'TCC revision mismatch: expected %s, got %s\n' \
        "$EXPECTED_REVISION" "$ACTUAL_REVISION" >&2
    exit 1
fi
if [ -n "$(git -C "$TCC_SOURCE" status --porcelain=v1)" ]; then
    printf 'TCC source checkout is dirty at revision %s\n' "$ACTUAL_REVISION" >&2
    exit 1
fi

case "$JOBS" in *[!0-9]*|'') printf 'invalid --jobs value: %s\n' "$JOBS" >&2; exit 2 ;; esac
[ "$JOBS" -gt 0 ] || { printf '%s\n' '--jobs must be greater than zero' >&2; exit 2; }

CC=${CROSS_PREFIX}gcc
AR=${CROSS_PREFIX}ar
READELF=${CROSS_PREFIX}readelf
for tool in "$CC" "$AR" "$READELF" make git file install; do
    command -v "$tool" >/dev/null 2>&1 || { printf 'required host tool not found: %s\n' "$tool" >&2; exit 1; }
done

if [ -e "$OUTPUT" ] && [ "$FORCE" -ne 1 ]; then
    printf 'refusing to overwrite existing output: %s\n' "$OUTPUT" >&2
    exit 1
fi

BUILD=$(mktemp -d "${TMPDIR:-/tmp}/vita-native-toolchain-build.XXXXXX")
OUT_PARENT=$(dirname "$OUTPUT")
mkdir -p "$OUT_PARENT"
PUBLISH=$(mktemp -d "$OUT_PARENT/.vita-native-toolchain.XXXXXX")
trap 'rm -rf "$BUILD" "$PUBLISH"' 0 HUP INT TERM
mkdir -p "$BUILD/src" "$PUBLISH/root"
cp -a "$TCC_SOURCE/." "$BUILD/src/"

# TinyCC normally generates a helper executable and runs it during the build.
# That cannot work when the helper was cross-built for ARM on an x86 host.
# config-predefs=no selects TCC's supported no-helper path instead.
(
    cd "$BUILD/src"
    ./configure \
        --prefix=/opt/vita-toolkit \
        --cpu=arm \
        --cross-prefix="$CROSS_PREFIX" \
        --cc=gcc --ar=ar \
        --sysroot=/opt/vita-toolkit/sysroot \
        --triplet=arm-linux-gnueabihf \
        --sysincludepaths='{B}/include:{R}/usr/include' \
        --libpaths='{B}:{R}/usr/lib' \
        --crtprefix='{R}/usr/lib' \
        --elfinterp=/lib/ld-linux-armhf.so.3 \
        --config-predefs=no \
        --extra-cflags='-O2 -static -march=armv7-a -mfpu=neon -mfloat-abi=hard' \
        --extra-ldflags='-static'
    make -j"$JOBS" tcc CC="$CC" AR="$AR" \
        CFLAGS='-O2 -static -march=armv7-a -mfpu=neon -mfloat-abi=hard' \
        LDFLAGS=-static
    make -C lib arm-libtcc1-usegcc=yes CC="$CC" AR="$AR" \
        CFLAGS='-O2 -march=armv7-a -mfpu=neon -mfloat-abi=hard'
)

TCC_BUILD="$BUILD/src/tcc"
LIBTCC1_BUILD="$BUILD/src/libtcc1.a"
[ -x "$TCC_BUILD" ] || { printf '%s\n' 'TinyCC build did not produce tcc' >&2; exit 1; }
[ -f "$LIBTCC1_BUILD" ] || { printf '%s\n' 'TinyCC build did not produce libtcc1.a' >&2; exit 1; }
file "$TCC_BUILD" | grep -q 'ELF 32-bit.*ARM' \
    || { printf '%s\n' 'TinyCC output is not a 32-bit ARM ELF' >&2; exit 1; }
if "$READELF" -l "$TCC_BUILD" | grep -q 'Requesting program interpreter'; then
    printf '%s\n' 'TinyCC output is dynamically linked; static compiler required' >&2
    exit 1
fi

DEST="$PUBLISH/root"
mkdir -p "$DEST/bin" "$DEST/lib/tcc/include" \
    "$DEST/sysroot/usr/include" "$DEST/sysroot/usr/lib" \
    "$DEST/share/native-toolchain"
chmod 0755 "$DEST" "$DEST/bin" "$DEST/lib" "$DEST/lib/tcc" \
    "$DEST/lib/tcc/include" "$DEST/sysroot" "$DEST/sysroot/usr" \
    "$DEST/sysroot/usr/include" "$DEST/sysroot/usr/lib" \
    "$DEST/share" "$DEST/share/native-toolchain"
install -m 0755 "$TCC_BUILD" "$DEST/bin/tcc"
install -m 0644 "$LIBTCC1_BUILD" "$DEST/lib/tcc/libtcc1.a"
cp -a "$BUILD/src/include/." "$DEST/lib/tcc/include/"
cp -a "$SYSROOT/include/." "$DEST/sysroot/usr/include/"
cp -a "$SYSROOT/lib/." "$DEST/sysroot/usr/lib/"

cat >"$DEST/share/native-toolchain/BUILD-INFO" <<EOF
tcc_revision=$ACTUAL_REVISION
tcc_target=arm-linux-gnueabihf
compiler_mount=/opt/vita-toolkit
sysroot=/opt/vita-toolkit/sysroot
elf_interpreter=/lib/ld-linux-armhf.so.3
compiler_linkage=static
EOF

hash_file() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    else
        shasum -a 256 "$1" | awk '{print $1}'
    fi
}
file_size() {
    stat -c '%s' "$1" 2>/dev/null || stat -f '%z' "$1"
}
file_mode() {
    stat -c '%a' "$1" 2>/dev/null || stat -f '%Lp' "$1"
}

MANIFEST="$DEST/NATIVE-TOOLCHAIN-MANIFEST"
: >"$MANIFEST"
(
    cd "$DEST"
    find . \( -type f -o -type l \) ! -name NATIVE-TOOLCHAIN-MANIFEST -print \
        | sed 's|^\./||' | LC_ALL=C sort
) | while IFS= read -r rel; do
    path=$DEST/$rel
    if [ -L "$path" ]; then
        target=$(readlink "$path")
        hash=$(printf '%s' "$target" | sha256sum | awk '{print $1}')
        size=${#target}
    else
        hash=$(hash_file "$path")
        size=$(file_size "$path")
    fi
    printf '%s %s %s %s\n' "$rel" "$(file_mode "$path")" "$size" "$hash" \
        >>"$MANIFEST"
done

# Publish the complete set only after every build and manifest step succeeds.
BACKUP=
if [ -e "$OUTPUT" ]; then
    BACKUP=$OUT_PARENT/.vita-native-toolchain.backup.$$
    mv "$OUTPUT" "$BACKUP"
fi
if mv "$DEST" "$OUTPUT"; then
    [ -z "$BACKUP" ] || rm -rf "$BACKUP"
else
    [ -z "$BACKUP" ] || mv "$BACKUP" "$OUTPUT"
    exit 1
fi

printf 'output=%s\n' "$OUTPUT"
printf 'tcc_revision=%s\n' "$ACTUAL_REVISION"
printf 'files=%s\n' "$(wc -l < "$OUTPUT/NATIVE-TOOLCHAIN-MANIFEST" | tr -d ' ')"
printf 'bytes=%s\n' "$(du -sk "$OUTPUT" | awk '{print $1 * 1024}')"
