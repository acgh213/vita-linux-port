#!/bin/sh
set -eu

ROOT=$(CDPATH='' cd -- "$(dirname "$0")/../.." && pwd)
BUILDER="$ROOT/tools/build-vita-toolkit-squashfs.sh"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' 0 HUP INT TERM

chmod +x "$BUILDER"
manifest=$("$BUILDER" --source "$ROOT/toolkit" --manifest-only)
printf '%s\n' "$manifest"
printf '%s\n' "$manifest" | grep -F 'VERSION ' >/dev/null
printf '%s\n' "$manifest" | grep -F 'bin/vita-diag ' >/dev/null
printf '%s\n' "$manifest" | grep -F 'bin/vita-netdiag ' >/dev/null
printf '%s\n' "$manifest" | grep -F 'bin/vita-toolkit-mount ' >/dev/null
printf '%s\n' "$manifest" | grep -F 'bin/vita-storage ' >/dev/null
printf '%s\n' "$manifest" | grep -F 'bin/vita-fb ' >/dev/null
printf '%s\n' "$manifest" | grep -F 'bin/vita-fbserve ' >/dev/null
printf '%s\n' "$manifest" | grep -F 'bin/vita-control ' >/dev/null
printf '%s\n' "$manifest" | grep -F 'bin/vita-usbinfo ' >/dev/null
printf '%s\n' "$manifest" | grep -F 'bin/vita-bench ' >/dev/null
printf '%s\n' "$manifest" | grep -F 'libexec/vita-bench.arm ' >/dev/null
printf '%s\n' "$manifest" | grep -F 'libexec/vita-fbserve.arm ' >/dev/null
printf '%s\n' "$manifest" | grep -F 'libexec/vita-control.arm ' >/dev/null
printf '%s\n' "$manifest" | grep -F 'share/vita-bench.c ' >/dev/null
printf '%s\n' "$manifest" | grep -F 'share/vita-fbserve.c ' >/dev/null
printf '%s\n' "$manifest" | grep -F 'share/vita-control.c ' >/dev/null
if printf '%s\n' "$manifest" | grep -E '\.ssh|wpa_supplicant|local/' >/dev/null; then
    printf 'FAIL: manifest contains forbidden local material\n' >&2
    exit 1
fi

dry_run=$("$BUILDER" --source "$ROOT/toolkit" --output "$TMP/vita-toolkit.squashfs" --dry-run)
printf '%s\n' "$dry_run" | grep -F -- '-comp zstd' >/dev/null
printf '%s\n' "$dry_run" | grep -F -- '-no-xattrs' >/dev/null
printf '%s\n' "$dry_run" | grep -F 'manifest:' >/dev/null

# A generated native-toolchain root is optional, but when supplied it must be
# authenticated by its own complete manifest before any file enters the image.
TOOLCHAIN="$TMP/native-toolchain"
mkdir -p "$TOOLCHAIN/bin" "$TOOLCHAIN/lib/tcc/include" \
    "$TOOLCHAIN/sysroot/usr/include" "$TOOLCHAIN/sysroot/usr/lib" \
    "$TOOLCHAIN/share/native-toolchain"
printf '#!/bin/sh\nexit 0\n' >"$TOOLCHAIN/bin/tcc"
chmod 0755 "$TOOLCHAIN/bin/tcc"
ln -s tcc "$TOOLCHAIN/bin/cc"
printf 'VITA_TOOLKIT_ROOT=/opt/vita-toolkit\n' >"$TOOLCHAIN/toolchain-env.sh"
printf 'runtime\n' >"$TOOLCHAIN/lib/tcc/libtcc1.a"
printf 'runmain\n' >"$TOOLCHAIN/lib/tcc/runmain.o"
printf 'internal\n' >"$TOOLCHAIN/lib/tcc/include/tccdefs.h"
printf 'builtins\n' >"$TOOLCHAIN/lib/tcc/include/tcclib.h"
printf 'stdio\n' >"$TOOLCHAIN/sysroot/usr/include/stdio.h"
printf 'crt\n' >"$TOOLCHAIN/sysroot/usr/lib/crt1.o"
printf 'libc\n' >"$TOOLCHAIN/sysroot/usr/lib/libc.so"
printf 'tcc_revision=fixture\n' >"$TOOLCHAIN/share/native-toolchain/BUILD-INFO"
(
    cd "$TOOLCHAIN"
    find . \( -type f -o -type l \) ! -name NATIVE-TOOLCHAIN-MANIFEST -print \
        | sed 's|^\./||' | LC_ALL=C sort | while IFS= read -r rel; do
        mode=$(stat -c '%a' "$rel" 2>/dev/null || stat -f '%Lp' "$rel")
        if [ -L "$rel" ]; then
            target=$(readlink "$rel")
            size=${#target}
            hash=$(printf '%s' "$target" | sha256sum | awk '{print $1}')
        else
            size=$(wc -c <"$rel" | tr -d ' ')
            hash=$(sha256sum "$rel" | awk '{print $1}')
        fi
        printf '%s %s %s %s\n' "$rel" "$mode" "$size" "$hash"
    done
) >"$TOOLCHAIN/NATIVE-TOOLCHAIN-MANIFEST"

toolchain_manifest=$("$BUILDER" --source "$ROOT/toolkit" \
    --toolchain-root "$TOOLCHAIN" --manifest-only)
for path in bin/tcc lib/tcc/libtcc1.a sysroot/usr/include/stdio.h \
            sysroot/usr/lib/libc.so NATIVE-TOOLCHAIN-MANIFEST; do
    printf '%s\n' "$toolchain_manifest" | grep -F "$path " >/dev/null \
        || { printf 'FAIL: payload manifest omits toolchain path %s\n' "$path" >&2; exit 1; }
done

# Stage-only mode publishes the exact verified root for environments where the
# agent safety layer forbids mksquashfs itself. It must not require mksquashfs.
STAGED="$TMP/staged-root"
MKSQUASHFS=definitely-missing "$BUILDER" --source "$ROOT/toolkit" \
    --toolchain-root "$TOOLCHAIN" --stage-output "$STAGED"
for path in MANIFEST NATIVE-TOOLCHAIN-MANIFEST bin/tcc bin/vita-diag \
            lib/tcc/libtcc1.a sysroot/usr/include/stdio.h; do
    [ -e "$STAGED/$path" ] || {
        printf 'FAIL: stage-only output omits %s\n' "$path" >&2
        exit 1
    }
done
[ "$(cat "$STAGED/MANIFEST")" = "$toolchain_manifest" ] \
    || { printf 'FAIL: staged MANIFEST differs from manifest-only output\n' >&2; exit 1; }
if "$BUILDER" --source "$ROOT/toolkit" --toolchain-root "$TOOLCHAIN" \
        --stage-output "$STAGED" >"$TMP/stage-existing.log" 2>&1; then
    printf 'FAIL: stage-only mode overwrote an existing root without --force\n' >&2
    exit 1
fi

# An unmanifested file must fail closed—even an innocuous name—because that is
# how credentials or host artifacts otherwise leak into a generated payload.
printf 'not declared\n' >"$TOOLCHAIN/unlisted-file"
if "$BUILDER" --source "$ROOT/toolkit" --toolchain-root "$TOOLCHAIN" \
        --manifest-only >"$TMP/unlisted.log" 2>&1; then
    printf 'FAIL: builder accepted an unmanifested toolchain file\n' >&2
    exit 1
fi
grep -qi 'manifest' "$TMP/unlisted.log" \
    || { printf 'FAIL: unmanifested-file rejection lacks a diagnostic\n' >&2; exit 1; }
rm -f "$TOOLCHAIN/unlisted-file"

# Manifest entries are content contracts, not merely allowlist names.
printf 'tampered\n' >>"$TOOLCHAIN/bin/tcc"
if "$BUILDER" --source "$ROOT/toolkit" --toolchain-root "$TOOLCHAIN" \
        --manifest-only >"$TMP/tampered.log" 2>&1; then
    printf 'FAIL: builder accepted a hash-mismatched toolchain file\n' >&2
    exit 1
fi
grep -qi 'hash\|manifest' "$TMP/tampered.log" \
    || { printf 'FAIL: hash rejection lacks a diagnostic\n' >&2; exit 1; }

printf 'not an image\n' > "$TMP/existing.squashfs"
if "$BUILDER" --source "$ROOT/toolkit" --output "$TMP/existing.squashfs" --dry-run >/dev/null 2>&1; then
    printf 'FAIL: dry-run accepted an existing output without --force\n' >&2
    exit 1
fi

printf 'squashfs builder safe-path tests passed\n'
