#!/bin/sh
# Build a reproducible Vita toolkit SquashFS payload.
set -eu

SOURCE=toolkit
OUTPUT=dist/vita-toolkit.squashfs
FORCE=0
MODE=build

usage() {
    printf '%s\n' \
        'usage: tools/build-vita-toolkit-squashfs.sh [options]' \
        '  --source DIR          toolkit source tree (default: toolkit)' \
        '  --output FILE         SquashFS output (default: dist/vita-toolkit.squashfs)' \
        '  --force               replace an existing output' \
        '  --dry-run             print the image command without running it' \
        '  --manifest-only       print the staged manifest and exit'
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --source)
            [ "$#" -ge 2 ] || { usage >&2; exit 2; }
            SOURCE=$2; shift 2 ;;
        --output)
            [ "$#" -ge 2 ] || { usage >&2; exit 2; }
            OUTPUT=$2; shift 2 ;;
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
[ -f "$SOURCE/squashfs/VERSION" ] || { printf 'missing source file: %s/squashfs/VERSION\n' "$SOURCE" >&2; exit 1; }

if [ "$MODE" != manifest ] && [ "$FORCE" -ne 1 ] && [ -e "$OUTPUT" ]; then
    printf 'refusing to overwrite existing output: %s\n' "$OUTPUT" >&2
    exit 1
fi
if [ "$MODE" != manifest ] && [ "$MODE" != dry-run ] && ! command -v mksquashfs >/dev/null 2>&1; then
    printf 'mksquashfs is required for image creation\n' >&2
    exit 1
fi

STAGE=$(mktemp -d "${TMPDIR:-/tmp}/vita-toolkit-stage.XXXXXX")
trap 'rm -rf "$STAGE"' 0 HUP INT TERM
mkdir -p "$STAGE/bin" "$STAGE/share"
chmod 0755 "$STAGE" "$STAGE/bin" "$STAGE/share"

install -m 0755 "$SOURCE/bin/vita-diag" "$STAGE/bin/vita-diag"
install -m 0755 "$SOURCE/bin/vita-netdiag" "$STAGE/bin/vita-netdiag"
install -m 0755 "$SOURCE/bin/vita-toolkit-mount" "$STAGE/bin/vita-toolkit-mount"
install -m 0644 "$SOURCE/squashfs/VERSION" "$STAGE/VERSION"
install -m 0644 "$SOURCE/README.md" "$STAGE/share/README.md"

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

MANIFEST=$STAGE/MANIFEST
: > "$MANIFEST"
for rel in VERSION bin/vita-diag bin/vita-netdiag bin/vita-toolkit-mount share/README.md; do
    file=$STAGE/$rel
    printf '%s %s %s %s\n' "$rel" "$(file_mode "$file")" \
        "$(file_size "$file")" "$(hash_file "$file")" >> "$MANIFEST"
done

if [ "$MODE" = manifest ]; then
    cat "$MANIFEST"
    exit 0
fi

mkdir -p "${OUTPUT%/*}"
if [ "${OUTPUT%/*}" = "$OUTPUT" ]; then
    mkdir -p .
fi
CMD="mksquashfs $STAGE $OUTPUT -noappend -all-root -no-xattrs -mkfs-time 0 -all-time 0 -processors 1 -comp zstd -Xcompression-level 19 -no-progress"
if [ "$MODE" = dry-run ]; then
    printf '%s\n' "$CMD"
    printf 'manifest:\n'
    cat "$MANIFEST"
    exit 0
fi

mksquashfs "$STAGE" "$OUTPUT" \
    -noappend -all-root -no-xattrs -mkfs-time 0 -all-time 0 \
    -processors 1 -comp zstd -Xcompression-level 19 -no-progress
printf 'output=%s\n' "$OUTPUT"
printf 'sha256=%s\n' "$(hash_file "$OUTPUT")"
printf 'bytes=%s\n' "$(file_size "$OUTPUT")"
