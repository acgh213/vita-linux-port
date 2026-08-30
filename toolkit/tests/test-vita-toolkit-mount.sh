#!/bin/sh
set -eu

ROOT=$(CDPATH='' cd -- "$(dirname "$0")/../.." && pwd)
TOOL="$ROOT/toolkit/bin/vita-toolkit-mount"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' 0 HUP INT TERM
chmod +x "$TOOL"

SOURCE="$TMP/vita-toolkit.squashfs"
TARGET="$TMP/mountpoint"
PROC="$TMP/mounts"
LOG="$TMP/log"
MOUNT="$TMP/mount"
UMOUNT="$TMP/umount"
printf 'image\n' > "$SOURCE"
: > "$PROC"
: > "$LOG"

cat > "$MOUNT" <<'EOF_MOUNT'
#!/bin/sh
printf '%s\n' "$*" >> "$VITA_TOOLKIT_TEST_LOG"
printf '%s %s squashfs ro,nosuid,nodev 0 0\n' "$5" "$6" > "$VITA_TOOLKIT_TEST_PROC"
EOF_MOUNT
cat > "$UMOUNT" <<'EOF_UMOUNT'
#!/bin/sh
printf '%s\n' "$*" >> "$VITA_TOOLKIT_TEST_LOG"
: > "$VITA_TOOLKIT_TEST_PROC"
EOF_UMOUNT
chmod +x "$MOUNT" "$UMOUNT"

if VITA_TOOLKIT_PROC_MOUNTS="$PROC" "$TOOL" --target "$TARGET" --machine >/dev/null 2>&1; then
    printf 'FAIL: missing --device was accepted\n' >&2
    exit 1
fi
if VITA_TOOLKIT_PROC_MOUNTS="$PROC" "$TOOL" --device "$SOURCE" --target / >/dev/null 2>&1; then
    printf 'FAIL: unsafe target was accepted\n' >&2
    exit 1
fi

mounted=$(VITA_TOOLKIT_PROC_MOUNTS="$PROC" \
    VITA_TOOLKIT_MOUNT_CMD="$MOUNT" \
    VITA_TOOLKIT_UMOUNT_CMD="$UMOUNT" \
    VITA_TOOLKIT_TEST_LOG="$LOG" \
    VITA_TOOLKIT_TEST_PROC="$PROC" \
    "$TOOL" --device "$SOURCE" --target "$TARGET" --machine)
printf '%s\n' "$mounted"
printf '%s\n' "$mounted" | grep -F 'status=mounted' >/dev/null
printf '%s\n' "$mounted" | grep -F 'filesystem=squashfs' >/dev/null
printf '%s\n' "$mounted" | grep -F 'options=ro,nosuid,nodev' >/dev/null
grep -F -- '-t squashfs -o ro,nosuid,nodev' "$LOG" >/dev/null

already=$(VITA_TOOLKIT_PROC_MOUNTS="$PROC" \
    VITA_TOOLKIT_MOUNT_CMD="$MOUNT" \
    VITA_TOOLKIT_UMOUNT_CMD="$UMOUNT" \
    VITA_TOOLKIT_TEST_LOG="$LOG" \
    VITA_TOOLKIT_TEST_PROC="$PROC" \
    "$TOOL" --device "$SOURCE" --target "$TARGET" --machine)
printf '%s\n' "$already" | grep -F 'status=already_mounted' >/dev/null

unmounted=$(VITA_TOOLKIT_PROC_MOUNTS="$PROC" \
    VITA_TOOLKIT_MOUNT_CMD="$MOUNT" \
    VITA_TOOLKIT_UMOUNT_CMD="$UMOUNT" \
    VITA_TOOLKIT_TEST_LOG="$LOG" \
    VITA_TOOLKIT_TEST_PROC="$PROC" \
    "$TOOL" --unmount --target "$TARGET" --machine)
printf '%s\n' "$unmounted" | grep -F 'status=unmounted' >/dev/null

file_mounted=$(VITA_TOOLKIT_PROC_MOUNTS="$PROC" \
    VITA_TOOLKIT_MOUNT_CMD="$MOUNT" \
    VITA_TOOLKIT_UMOUNT_CMD="$UMOUNT" \
    VITA_TOOLKIT_TEST_LOG="$LOG" \
    VITA_TOOLKIT_TEST_PROC="$PROC" \
    "$TOOL" --file "$SOURCE" --target "$TARGET" --machine)
printf '%s\n' "$file_mounted" | grep -F 'source_type=file' >/dev/null
printf '%s\n' "$file_mounted" | grep -F 'options=loop,ro,nosuid,nodev' >/dev/null
grep -F -- '-t squashfs -o loop,ro,nosuid,nodev' "$LOG" >/dev/null
printf 'vita-toolkit-mount test passed\n'
