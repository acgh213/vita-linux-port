#!/bin/sh
# Behavioral contract for tools/build-vita-native-toolchain.sh.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
BUILDER="$ROOT/tools/build-vita-native-toolchain.sh"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' 0 HUP INT TERM

fail() {
    printf 'FAIL: %s\n' "$1" >&2
    exit 1
}

[ -x "$BUILDER" ] || fail 'native toolchain builder is missing or not executable'
command -v arm-linux-gnueabihf-gcc >/dev/null 2>&1 \
    || fail 'arm-linux-gnueabihf-gcc is required by the fixture'

# A tiny TCC-shaped source fixture. It exercises the REAL builder commands and
# produces real ARM artifacts, while avoiding a network clone in the unit test.
SRC="$TMP/tcc-source"
mkdir -p "$SRC/include" "$SRC/lib"
cat >"$SRC/tiny-tcc.c" <<'EOF'
#include <stdio.h>
int main(void) { puts("fixture tcc"); return 0; }
EOF
cat >"$SRC/runtime.c" <<'EOF'
int __fixture_runtime(void) { return 1; }
EOF
printf '/* fixture internal header */\n' >"$SRC/include/tccdefs.h"
cat >"$SRC/configure" <<'EOF'
#!/bin/sh
# The production builder must pass every hard-float/sysroot contract option.
args=" $* "
for required in \
  '--cpu=arm' \
  '--triplet=arm-linux-gnueabihf' \
  '--sysroot=/opt/vita-toolkit/sysroot' \
  '--elfinterp=/lib/ld-linux-armhf.so.3' \
  '--config-predefs=no'
do
    case "$args" in
        *" $required "*) : ;;
        *) printf 'missing configure option: %s\n' "$required" >&2; exit 91 ;;
    esac
done
# Real TCC configures in its source tree. This fixture already carries the
# Makefiles, so successful option validation is sufficient.
exit 0
EOF
chmod +x "$SRC/configure"
cat >"$SRC/Makefile" <<'EOF'
tcc: tiny-tcc.c
	$(CC) $(CFLAGS) -static -o $@ $<
EOF
cat >"$SRC/lib/Makefile" <<'EOF'
all:
	$(CC) $(CFLAGS) -c ../runtime.c -o runtime.o
	$(AR) rcs ../libtcc1.a runtime.o
EOF

git -C "$SRC" init -q
git -C "$SRC" -c user.name=fixture -c user.email=fixture.invalid add .
git -C "$SRC" -c user.name=fixture -c user.email=fixture.invalid commit -qm fixture
REV=$(git -C "$SRC" rev-parse HEAD)
BEFORE=$(git -C "$SRC" status --porcelain=v1)

SYSROOT="$TMP/sysroot"
mkdir -p "$SYSROOT/include" "$SYSROOT/lib"
printf '/* stdio fixture */\n' >"$SYSROOT/include/stdio.h"
printf 'crt fixture\n' >"$SYSROOT/lib/crt1.o"
printf 'libc fixture\n' >"$SYSROOT/lib/libc.so"
printf 'loader fixture\n' >"$SYSROOT/lib/ld-linux-armhf.so.3"

OUT="$TMP/output"
"$BUILDER" \
    --tcc-source "$SRC" \
    --sysroot "$SYSROOT" \
    --expected-revision "$REV" \
    --output "$OUT"

after=$(git -C "$SRC" status --porcelain=v1)
[ "$BEFORE" = "$after" ] || fail 'builder modified the pinned TCC source tree'

# Exact installed layout: payload root itself mounts at /opt/vita-toolkit.
for path in \
    bin/tcc \
    lib/tcc/libtcc1.a \
    lib/tcc/include/tccdefs.h \
    sysroot/usr/include/stdio.h \
    sysroot/usr/lib/crt1.o \
    sysroot/usr/lib/libc.so \
    share/native-toolchain/BUILD-INFO \
    NATIVE-TOOLCHAIN-MANIFEST
 do
    [ -f "$OUT/$path" ] || fail "missing staged path: $path"
done
[ -x "$OUT/bin/tcc" ] || fail 'staged tcc is not executable'
file "$OUT/bin/tcc" | grep -q 'ELF 32-bit.*ARM' \
    || fail 'staged tcc is not a 32-bit ARM ELF'
arm-linux-gnueabihf-readelf -l "$OUT/bin/tcc" | grep -qv 'Requesting program interpreter' \
    || fail 'staged tcc must be static'

# Manifest is deterministic, sorted, complete, and contains no host paths or
# credentials. Every ordinary file except the manifest itself is represented.
LC_ALL=C sort -c "$OUT/NATIVE-TOOLCHAIN-MANIFEST" \
    || fail 'native toolchain manifest is not sorted'
for path in bin/tcc lib/tcc/libtcc1.a sysroot/usr/include/stdio.h \
            sysroot/usr/lib/libc.so share/native-toolchain/BUILD-INFO; do
    grep -F "$path " "$OUT/NATIVE-TOOLCHAIN-MANIFEST" >/dev/null \
        || fail "manifest omits $path"
done
if grep -E "$TMP|\.git/|\.ssh|wpa_supplicant|local/" \
        "$OUT/NATIVE-TOOLCHAIN-MANIFEST" >/dev/null; then
    fail 'manifest leaks host paths or forbidden local material'
fi

# Wrong source revision must fail closed and publish no output.
BAD="$TMP/bad-output"
if "$BUILDER" --tcc-source "$SRC" --sysroot "$SYSROOT" \
        --expected-revision 0000000000000000000000000000000000000000 \
        --output "$BAD" >"$TMP/bad.log" 2>&1; then
    fail 'builder accepted the wrong TCC revision'
fi
[ ! -e "$BAD" ] || fail 'revision failure published an output tree'
grep -qi 'revision' "$TMP/bad.log" || fail 'revision failure lacks a diagnostic'

# Existing output is never replaced unless the caller explicitly opts in.
printf 'sentinel\n' >"$OUT/sentinel"
if "$BUILDER" --tcc-source "$SRC" --sysroot "$SYSROOT" \
        --expected-revision "$REV" --output "$OUT" \
        >"$TMP/existing.log" 2>&1; then
    fail 'builder overwrote an existing output without --force'
fi
[ "$(cat "$OUT/sentinel")" = sentinel ] \
    || fail 'existing output changed after refused rebuild'

printf 'native toolchain builder contract tests passed\n'
