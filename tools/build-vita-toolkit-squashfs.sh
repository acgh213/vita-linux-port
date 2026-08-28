#!/bin/sh
# Build a reproducible Vita toolkit SquashFS payload.
set -eu

SOURCE=toolkit
OUTPUT=dist/vita-toolkit.squashfs
STAGE_OUTPUT=
TOOLCHAIN_ROOT=
VERIFY=
PUBLISH=
MKSQUASHFS=${MKSQUASHFS:-mksquashfs}
FORCE=0
MODE=build

usage() {
    printf '%s\n' \
        'usage: tools/build-vita-toolkit-squashfs.sh [options]' \
        '  --source DIR          toolkit source tree (default: toolkit)' \
        '  --toolchain-root DIR  verified generated native-toolchain tree' \
        '  --output FILE         SquashFS output (default: dist/vita-toolkit.squashfs)' \
        '  --stage-output DIR    publish verified root without creating SquashFS' \
        '  --force               replace an existing output' \
        '  --dry-run             print the image command without running it' \
        '  --manifest-only       print the staged manifest and exit'
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --source)
            [ "$#" -ge 2 ] || { usage >&2; exit 2; }
            SOURCE=$2; shift 2 ;;
        --toolchain-root)
            [ "$#" -ge 2 ] || { usage >&2; exit 2; }
            TOOLCHAIN_ROOT=$2; shift 2 ;;
        --output)
            [ "$#" -ge 2 ] || { usage >&2; exit 2; }
            OUTPUT=$2; shift 2 ;;
        --stage-output)
            [ "$#" -ge 2 ] || { usage >&2; exit 2; }
            STAGE_OUTPUT=$2; MODE=stage; shift 2 ;;
        --force) FORCE=1; shift ;;
        --dry-run) MODE=dry-run; shift ;;
        --manifest-only) MODE=manifest; shift ;;
        -h|--help) usage; exit 0 ;;
        *) printf 'unknown option: %s\n' "$1" >&2; usage >&2; exit 2 ;;
    esac
done

[ -d "$SOURCE" ] || { printf 'missing source: %s\n' "$SOURCE" >&2; exit 1; }
[ -f "$SOURCE/bin/vita-diag" ] || { printf 'missing source file: %s/bin/vita-diag\n' "$SOURCE" >&2; exit 1; }
[ -f "$SOURCE/bin/vita-netdiag" ] || { printf 'missing source file: %s/bin/vita-netdiag\n' "$SOURCE" >&2; exit 1; }
[ -f "$SOURCE/bin/vita-toolkit-mount" ] || { printf 'missing source file: %s/bin/vita-toolkit-mount\n' "$SOURCE" >&2; exit 1; }
[ -f "$SOURCE/bin/vita-storage" ] || { printf 'missing source file: %s/bin/vita-storage\n' "$SOURCE" >&2; exit 1; }
[ -f "$SOURCE/squashfs/VERSION" ] || { printf 'missing source file: %s/squashfs/VERSION\n' "$SOURCE" >&2; exit 1; }

hash_file() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    else
        shasum -a 256 "$1" | awk '{print $1}'
    fi
}

hash_text() {
    if command -v sha256sum >/dev/null 2>&1; then
        printf '%s' "$1" | sha256sum | awk '{print $1}'
    else
        printf '%s' "$1" | shasum -a 256 | awk '{print $1}'
    fi
}

file_size() {
    stat -c '%s' "$1" 2>/dev/null || stat -f '%z' "$1"
}

file_mode() {
    stat -c '%a' "$1" 2>/dev/null || stat -f '%Lp' "$1"
}

if [ -n "$TOOLCHAIN_ROOT" ]; then
    [ -d "$TOOLCHAIN_ROOT" ] || { printf 'missing toolchain root: %s\n' "$TOOLCHAIN_ROOT" >&2; exit 1; }
    TOOLCHAIN_MANIFEST=$TOOLCHAIN_ROOT/NATIVE-TOOLCHAIN-MANIFEST
    [ -f "$TOOLCHAIN_MANIFEST" ] || { printf 'missing toolchain manifest: %s\n' "$TOOLCHAIN_MANIFEST" >&2; exit 1; }
    for required in bin/tcc bin/cc toolchain-env.sh lib/tcc/libtcc1.a \
                    lib/tcc/runmain.o lib/tcc/include/tccdefs.h lib/tcc/include/tcclib.h \
                    sysroot/usr/include/stdio.h sysroot/usr/lib/crt1.o \
                    sysroot/usr/lib/libc.so share/native-toolchain/BUILD-INFO; do
        [ -e "$TOOLCHAIN_ROOT/$required" ] || { printf 'toolchain manifest root lacks %s\n' "$required" >&2; exit 1; }
    done

    VERIFY=$(mktemp -d "${TMPDIR:-/tmp}/vita-toolchain-verify.XXXXXX")
    trap 'rm -rf "$VERIFY"' 0 HUP INT TERM
    : >"$VERIFY/listed"
    previous=
    while IFS=' ' read -r rel expected_mode expected_size expected_hash extra; do
        if ! { [ -n "$rel" ] && [ -n "$expected_mode" ] \
                && [ -n "$expected_size" ] && [ -n "$expected_hash" ] \
                && [ -z "$extra" ]; }; then
            printf 'malformed toolchain manifest entry: %s\n' "$rel" >&2
            exit 1
        fi
        case "$rel" in
            /*|..|../*|*/../*|*/..|*" "*|*"	"*)
                printf 'unsafe toolchain manifest path: %s\n' "$rel" >&2; exit 1 ;;
        esac
        [ "$rel" != "$previous" ] || { printf 'duplicate toolchain manifest path: %s\n' "$rel" >&2; exit 1; }
        previous=$rel
        path=$TOOLCHAIN_ROOT/$rel
        [ -f "$path" ] || [ -L "$path" ] \
            || { printf 'toolchain manifest path is missing: %s\n' "$rel" >&2; exit 1; }
        if [ -L "$path" ]; then
            target=$(readlink "$path")
            case "$target" in
                /*|..|../*|*/../*|*/..)
                    printf 'unsafe toolchain symlink: %s -> %s\n' "$rel" "$target" >&2; exit 1 ;;
            esac
            actual_size=${#target}
            actual_hash=$(hash_text "$target")
        else
            actual_size=$(file_size "$path")
            actual_hash=$(hash_file "$path")
        fi
        actual_mode=$(file_mode "$path")
        [ "$actual_mode" = "$expected_mode" ] \
            || { printf 'toolchain manifest mode mismatch: %s\n' "$rel" >&2; exit 1; }
        [ "$actual_size" = "$expected_size" ] \
            || { printf 'toolchain manifest size mismatch: %s\n' "$rel" >&2; exit 1; }
        [ "$actual_hash" = "$expected_hash" ] \
            || { printf 'toolchain manifest hash mismatch: %s\n' "$rel" >&2; exit 1; }
        printf '%s\n' "$rel" >>"$VERIFY/listed"
    done <"$TOOLCHAIN_MANIFEST"
    LC_ALL=C sort -c "$VERIFY/listed" 2>/dev/null \
        || { printf '%s\n' 'toolchain manifest is not sorted' >&2; exit 1; }
    (
        cd "$TOOLCHAIN_ROOT"
        find . \( -type f -o -type l \) ! -name NATIVE-TOOLCHAIN-MANIFEST -print \
            | sed 's|^\./||' | LC_ALL=C sort
    ) >"$VERIFY/actual"
    cmp -s "$VERIFY/listed" "$VERIFY/actual" \
        || { printf '%s\n' 'toolchain manifest does not cover the exact file set' >&2; exit 1; }
    if grep -E '(^|/)(\.git|\.ssh|local)(/|$)|wpa_supplicant' "$VERIFY/listed" >/dev/null; then
        printf '%s\n' 'toolchain manifest contains forbidden local material' >&2
        exit 1
    fi
fi

case "$MODE" in
    build|dry-run)
        if [ "$FORCE" -ne 1 ] && [ -e "$OUTPUT" ]; then
            printf 'refusing to overwrite existing output: %s\n' "$OUTPUT" >&2
            exit 1
        fi ;;
    stage)
        [ -n "$STAGE_OUTPUT" ] || { printf '%s\n' 'missing stage output path' >&2; exit 2; }
        if [ "$FORCE" -ne 1 ] && [ -e "$STAGE_OUTPUT" ]; then
            printf 'refusing to overwrite existing stage output: %s\n' "$STAGE_OUTPUT" >&2
            exit 1
        fi ;;
esac
if [ "$MODE" = build ] && ! command -v "$MKSQUASHFS" >/dev/null 2>&1; then
    printf 'mksquashfs is required for image creation: %s\n' "$MKSQUASHFS" >&2
    exit 1
fi

STAGE=$(mktemp -d "${TMPDIR:-/tmp}/vita-toolkit-stage.XXXXXX")
trap 'rm -rf "$VERIFY" "$STAGE" "$PUBLISH"' 0 HUP INT TERM
mkdir -p "$STAGE/bin" "$STAGE/libexec" "$STAGE/share"
chmod 0755 "$STAGE" "$STAGE/bin" "$STAGE/share"

install -m 0755 "$SOURCE/bin/vita-diag" "$STAGE/bin/vita-diag"
install -m 0755 "$SOURCE/bin/vita-netdiag" "$STAGE/bin/vita-netdiag"
install -m 0755 "$SOURCE/bin/vita-toolkit-mount" "$STAGE/bin/vita-toolkit-mount"
install -m 0755 "$SOURCE/bin/vita-storage" "$STAGE/bin/vita-storage"
install -m 0755 "$SOURCE/bin/vita-fb" "$STAGE/bin/vita-fb"
install -m 0755 "$SOURCE/bin/vita-bench" "$STAGE/bin/vita-bench"
install -m 0755 "$SOURCE/bin/vita-bench.arm" "$STAGE/libexec/vita-bench.arm"
install -m 0644 "$SOURCE/squashfs/VERSION" "$STAGE/VERSION"
install -m 0644 "$SOURCE/README.md" "$STAGE/share/README.md"
install -m 0644 "$SOURCE/share/vita-bench.c" "$STAGE/share/vita-bench.c"

if [ -n "$TOOLCHAIN_ROOT" ]; then
    while IFS= read -r rel; do
        if [ -e "$STAGE/$rel" ] || [ -L "$STAGE/$rel" ]; then
            printf 'toolchain path collides with toolkit payload: %s\n' "$rel" >&2
            exit 1
        fi
    done <"$VERIFY/listed"
    cp -a "$TOOLCHAIN_ROOT/." "$STAGE/"
fi

MANIFEST=$STAGE/MANIFEST
: >"$MANIFEST"
(
    cd "$STAGE"
    find . \( -type f -o -type l \) ! -name MANIFEST -print \
        | sed 's|^\./||' | LC_ALL=C sort
) | while IFS= read -r rel; do
    file=$STAGE/$rel
    if [ -L "$file" ]; then
        target=$(readlink "$file")
        hash=$(hash_text "$target")
        size=${#target}
    else
        hash=$(hash_file "$file")
        size=$(file_size "$file")
    fi
    printf '%s %s %s %s\n' "$rel" "$(file_mode "$file")" "$size" "$hash" \
        >>"$MANIFEST"
done

if [ "$MODE" = manifest ]; then
    cat "$MANIFEST"
    exit 0
fi

if [ "$MODE" = stage ]; then
    STAGE_PARENT=$(dirname "$STAGE_OUTPUT")
    mkdir -p "$STAGE_PARENT"
    PUBLISH=$(mktemp -d "$STAGE_PARENT/.vita-toolkit-stage.XXXXXX")
    mkdir -p "$PUBLISH/root"
    cp -a "$STAGE/." "$PUBLISH/root/"
    BACKUP=
    if [ -e "$STAGE_OUTPUT" ]; then
        BACKUP=$STAGE_PARENT/.vita-toolkit-stage.backup.$$
        mv "$STAGE_OUTPUT" "$BACKUP"
    fi
    if mv "$PUBLISH/root" "$STAGE_OUTPUT"; then
        [ -z "$BACKUP" ] || rm -rf "$BACKUP"
    else
        [ -z "$BACKUP" ] || mv "$BACKUP" "$STAGE_OUTPUT"
        exit 1
    fi
    printf 'stage_output=%s\n' "$STAGE_OUTPUT"
    printf 'files=%s\n' "$(wc -l < "$STAGE_OUTPUT/MANIFEST" | tr -d ' ')"
    exit 0
fi

mkdir -p "${OUTPUT%/*}"
if [ "${OUTPUT%/*}" = "$OUTPUT" ]; then
    mkdir -p .
fi
CMD="$MKSQUASHFS $STAGE $OUTPUT -noappend -all-root -no-xattrs -mkfs-time 0 -all-time 0 -processors 1 -comp zstd -Xcompression-level 19 -no-progress"
if [ "$MODE" = dry-run ]; then
    printf '%s\n' "$CMD"
    printf 'manifest:\n'
    cat "$MANIFEST"
    exit 0
fi

"$MKSQUASHFS" "$STAGE" "$OUTPUT" \
    -noappend -all-root -no-xattrs -mkfs-time 0 -all-time 0 \
    -processors 1 -comp zstd -Xcompression-level 19 -no-progress
printf 'output=%s\n' "$OUTPUT"
printf 'sha256=%s\n' "$(hash_file "$OUTPUT")"
printf 'bytes=%s\n' "$(file_size "$OUTPUT")"
