#!/usr/bin/env bash
# vita-manifest.sh — one-command provenance/deploy manifest for the Vita/PSTV
# Linux port (roadmap Task C3).
#
# Emits a machine-readable JSON manifest covering source provenance, artifact
# hashes/sizes, embedded-initramfs identity, and (optionally, offline-only by
# default) remote readback comparison — and FAILS CLOSED (non-zero exit) on
# any of the conditions the roadmap and vita-linux-development SKILL.md
# "Critical pitfalls" list call out as silent-corruption risks:
#
#   - a dirty, unexplained build tree (kernel and/or outer repo)
#   - a missing/unpopulated private overlay (buildroot-vita/board/vita/local)
#   - a rootfs-less zImage (CONFIG_INITRAMFS_SOURCE unset/empty, or a zImage
#     too small to plausibly contain an embedded rootfs)
#   - a DTB that fails to decompile, or (when a staged copy is given) does
#     not byte-match the tree's build output
#   - a failed remote-readback comparison (offline, from already-pulled
#     files, or explicitly-invoked live SSH readback — never both silently)
#   - dev/console in the rootfs archive not being a character device 5:1
#
# The embedded initramfs is located ONLY via the __initramfs_start /
# __initramfs_size ELF symbols and their containing ELF section's
# (Addr, Off) pair (tools/lib/vita-extract-initramfs.py). Locating it by
# scanning for zstd magic bytes is explicitly forbidden — this rootfs
# contains internal zstd frames and a magic search finds a truncated
# internal frame instead of the real embedded image (roadmap governing
# rule 2; lab/pstv-recovery-2026-08-29/RESULT.md).
#
# HARD SAFETY NOTE: this tool never touches hardware unless invoked with
# --remote-host AND --remote-readback-live together. That path is fully
# implemented (remote_readback_live) but is not exercised by any test in
# this repository's history — see c3-report.md.
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)
EXTRACT_PY="$SCRIPT_DIR/lib/vita-extract-initramfs.py"
RENDER_PY="$SCRIPT_DIR/lib/vita-manifest-render.py"

# ---------------------------------------------------------------- defaults
KERNEL_DIR=""
OUTER_DIR="$(pwd)"
MODELS="pstv"
PAYLOAD=""
KPLUGIN=""
ROOTFS_SOURCE=""
EXPECT_ROOTFS_SHA256=""
STAGED_DTBS=()
READBACK_DIR=""
REMOTE_HOST=""
REMOTE_READBACK_LIVE=0
REMOTE_SSH_USER="root"
REMOTE_ZIMAGE_PATH="ux0:/linux/zImage"
REMOTE_DTB_PATH=""
BOOT_LOG=""
TARGET_MODEL=""
TARGET_FIRMWARE=""
MIN_ZIMAGE_BYTES=$((20 * 1024 * 1024))
DIRTY_ALLOWLIST=""
OUT_JSON=""
NM_BIN="nm"
READELF_BIN="readelf"

usage() {
    cat <<'EOF'
usage: vita-manifest.sh --kernel-dir DIR [options]

Required:
  --kernel-dir DIR           kernel build tree (holds .config, vmlinux,
                              arch/arm/boot/{zImage,dts/*.dtb},
                              rootfs.cpio.zst source convenience copy)

Common options:
  --outer-dir DIR            outer repo (default: cwd)
  --model NAME[,NAME...]     DTB model(s) to check: vita1000|vita2000|pstv
                              (default: pstv). "all" expands to all three.
  --payload FILE             payload.bin to hash
  --kplugin FILE             kplugin(.skprx) to hash
  --rootfs-source FILE       source rootfs.cpio.zst (default:
                              <kernel-dir>/rootfs.cpio.zst if present)
  --expect-rootfs-sha256 SHA expected embedded-initramfs SHA-256 (in
                              addition to / instead of --rootfs-source)
  --staged-dtb MODEL=FILE    a staged/about-to-deploy DTB to byte-compare
                              against the tree's build output (repeatable)
  --dirty-allowlist FILE     file of `git status --porcelain` lines that
                              are pre-approved as "explained" dirt
  --out FILE                 write JSON manifest here (default: stdout)

Boot/target scaffolding (recorded, not verified against hardware):
  --target-model NAME        recorded target model string
  --target-firmware STRING   recorded target firmware string
  --boot-log FILE            offline-parse an existing boot/transition log

Remote readback (OFFLINE by default — see below):
  --readback-dir DIR         directory holding already-pulled-back files
                              (e.g. zImage, <model>.dtb) to compare against
                              local build output. Does NOT touch the network.
  --remote-host HOST         hardware host for LIVE SSH/FTP readback.
                              Requires --remote-readback-live as well; by
                              itself this does nothing. NEVER pass both of
                              these unless you intend to contact real
                              hardware right now.
  --remote-readback-live     actually perform the live SSH readback in
                              remote_readback_live(). Implemented but
                              intentionally never invoked by any test in
                              this repository's history (see c3-report.md).
  --remote-ssh-user USER     default: root
  --remote-zimage-path PATH  remote zImage path (default: ux0:/linux/zImage)
  --remote-dtb-path PATH     remote DTB path (required with --remote-host)

Misc:
  --min-zimage-bytes N       deployable-size floor (default: 20971520 = 20MB)
  --nm BIN / --readelf BIN   override nm/readelf (e.g. for cross tools)
  -h, --help                 show this help

Exit status: 0 if every required gate PASSed; 1 if any required gate
FAILed; 2 on a usage/precondition error before any gate could run.
EOF
}

# --------------------------------------------------------------- arg parse
while [ "$#" -gt 0 ]; do
    case "$1" in
        --kernel-dir) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; KERNEL_DIR=$2; shift 2 ;;
        --outer-dir) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; OUTER_DIR=$2; shift 2 ;;
        --model) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; MODELS=$2; shift 2 ;;
        --payload) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; PAYLOAD=$2; shift 2 ;;
        --kplugin) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; KPLUGIN=$2; shift 2 ;;
        --rootfs-source) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; ROOTFS_SOURCE=$2; shift 2 ;;
        --expect-rootfs-sha256) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; EXPECT_ROOTFS_SHA256=$2; shift 2 ;;
        --staged-dtb) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; STAGED_DTBS+=("$2"); shift 2 ;;
        --dirty-allowlist) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; DIRTY_ALLOWLIST=$2; shift 2 ;;
        --out) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; OUT_JSON=$2; shift 2 ;;
        --target-model) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; TARGET_MODEL=$2; shift 2 ;;
        --target-firmware) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; TARGET_FIRMWARE=$2; shift 2 ;;
        --boot-log) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; BOOT_LOG=$2; shift 2 ;;
        --readback-dir) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; READBACK_DIR=$2; shift 2 ;;
        --remote-host) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; REMOTE_HOST=$2; shift 2 ;;
        --remote-readback-live) REMOTE_READBACK_LIVE=1; shift ;;
        --remote-ssh-user) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; REMOTE_SSH_USER=$2; shift 2 ;;
        --remote-zimage-path) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; REMOTE_ZIMAGE_PATH=$2; shift 2 ;;
        --remote-dtb-path) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; REMOTE_DTB_PATH=$2; shift 2 ;;
        --min-zimage-bytes) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; MIN_ZIMAGE_BYTES=$2; shift 2 ;;
        --nm) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; NM_BIN=$2; shift 2 ;;
        --readelf) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; READELF_BIN=$2; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) printf 'unknown option: %s\n' "$1" >&2; usage >&2; exit 2 ;;
    esac
done

if [ -z "$KERNEL_DIR" ]; then
    printf 'error: --kernel-dir is required\n' >&2
    usage >&2
    exit 2
fi
if [ ! -d "$KERNEL_DIR" ]; then
    printf 'error: --kernel-dir does not exist: %s\n' "$KERNEL_DIR" >&2
    exit 2
fi
if [ ! -d "$OUTER_DIR" ]; then
    printf 'error: --outer-dir does not exist: %s\n' "$OUTER_DIR" >&2
    exit 2
fi

WORKDIR=$(mktemp -d "${TMPDIR:-/tmp}/vita-manifest.XXXXXX")
# shellcheck disable=SC2317  # invoked indirectly via `trap ... EXIT`
cleanup() { rm -rf -- "$WORKDIR"; }
trap cleanup EXIT

FACTS_FILE="$WORKDIR/facts.jsonl"
: > "$FACTS_FILE"

RESULT_NAMES=()
RESULT_STATUS=()
RESULT_DETAIL=()

record() {
    # record NAME STATUS DETAIL   (STATUS: PASS|FAIL|SKIP)
    RESULT_NAMES+=("$1")
    RESULT_STATUS+=("$2")
    RESULT_DETAIL+=("$3")
    fact_append json "$(build_check_json "$1" "$2" "$3")" checks
}

build_check_json() {
    python3 - "$1" "$2" "$3" <<'PY'
import json, sys
name, status, detail = sys.argv[1:4]
print(json.dumps({"name": name, "status": status, "detail": detail}))
PY
}

fact_set() {
    # fact_set TYPE VALUE path.dotted.key
    local vtype="$1" value="$2" dotted="$3"
    python3 - "$vtype" "$value" "$dotted" <<'PY' >> "$FACTS_FILE"
import json, sys
vtype, value, dotted = sys.argv[1:4]
if vtype == "str":
    v = value
elif vtype == "int":
    v = int(value)
elif vtype == "bool":
    v = value == "true"
elif vtype == "json":
    v = json.loads(value)
else:
    raise SystemExit("bad vtype: %s" % vtype)
print(json.dumps({"path": dotted.split("."), "op": "set", "value": v}))
PY
}

fact_append() {
    # fact_append TYPE VALUE path.dotted.key
    local vtype="$1" value="$2" dotted="$3"
    python3 - "$vtype" "$value" "$dotted" <<'PY' >> "$FACTS_FILE"
import json, sys
vtype, value, dotted = sys.argv[1:4]
if vtype == "json":
    v = json.loads(value)
else:
    v = value
print(json.dumps({"path": dotted.split("."), "op": "append", "value": v}))
PY
}

sha256_of() { sha256sum -- "$1" | awk '{print $1}'; }
size_of() { stat -c '%s' -- "$1"; }

# ------------------------------------------------------------ 1. git state
git_state() {
    local dir="$1" label="$2"
    if ! git -C "$dir" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        record "git:$label" FAIL "not a git work tree: $dir"
        fact_set str "" "git.$label.commit"
        return
    fi
    local commit dirty
    commit=$(git -C "$dir" rev-parse HEAD)
    dirty=$(git -C "$dir" status --porcelain 2>/dev/null || true)
    fact_set str "$commit" "git.$label.commit"
    fact_set str "$dir" "git.$label.path"

    if [ -z "$dirty" ]; then
        fact_set bool true "git.$label.clean"
        fact_set json '[]' "git.$label.dirty_entries"
        record "git:$label:clean" PASS "HEAD=$commit clean"
        return
    fi

    local unexplained="$dirty"
    if [ -n "$DIRTY_ALLOWLIST" ] && [ -f "$DIRTY_ALLOWLIST" ]; then
        unexplained=$(comm -23 <(printf '%s\n' "$dirty" | sort) <(sort -- "$DIRTY_ALLOWLIST") || true)
    fi

    local dirty_json
    dirty_json=$(printf '%s\n' "$dirty" | python3 -c 'import json,sys; print(json.dumps([l for l in sys.stdin.read().splitlines() if l]))')
    fact_set json "$dirty_json" "git.$label.dirty_entries"
    fact_set bool false "git.$label.clean"

    if [ -z "$unexplained" ]; then
        record "git:$label:clean" PASS "HEAD=$commit dirty but every entry is on the allowlist ($DIRTY_ALLOWLIST)"
    else
        record "git:$label:clean" FAIL "HEAD=$commit dirty, unexplained: $(printf '%s' "$unexplained" | tr '\n' ';')"
    fi
}

git_state "$KERNEL_DIR" kernel
git_state "$OUTER_DIR" outer

# --------------------------------------------------- 2. private overlay
OVERLAY_DIR="$OUTER_DIR/buildroot-vita/board/vita/local"
overlay_check() {
    if [ ! -d "$OVERLAY_DIR" ]; then
        record "overlay:present" FAIL "missing overlay directory: $OVERLAY_DIR"
        return
    fi
    record "overlay:present" PASS "$OVERLAY_DIR exists"

    local required_files=(
        "etc/wpa_supplicant.conf"
        "root/.ssh/authorized_keys"
        "etc/ssh/ssh_host_ed25519_key"
        "etc/ssh/ssh_host_ed25519_key.pub"
        "etc/ssh/ssh_host_rsa_key"
        "etc/ssh/ssh_host_rsa_key.pub"
        "etc/ssh/ssh_host_ecdsa_key"
        "etc/ssh/ssh_host_ecdsa_key.pub"
    )
    local private_keys=(
        "etc/ssh/ssh_host_ed25519_key"
        "etc/ssh/ssh_host_rsa_key"
        "etc/ssh/ssh_host_ecdsa_key"
    )
    local rel path size mode all_ok=1
    for rel in "${required_files[@]}"; do
        path="$OVERLAY_DIR/$rel"
        if [ ! -f "$path" ]; then
            record "overlay:file:$rel" FAIL "missing: $rel"
            all_ok=0
            continue
        fi
        size=$(size_of "$path")
        if [ "$size" -le 0 ]; then
            record "overlay:file:$rel" FAIL "empty: $rel"
            all_ok=0
            continue
        fi
        # Hash only — never print/log file contents (secrets stay secret).
        fact_set str "$(sha256_of "$path")" "overlay.files.$(printf '%s' "$rel" | tr '/.' '__').sha256"
        fact_set int "$size" "overlay.files.$(printf '%s' "$rel" | tr '/.' '__').size"
        record "overlay:file:$rel" PASS "present, ${size}B"
    done
    for rel in "${private_keys[@]}"; do
        path="$OVERLAY_DIR/$rel"
        [ -f "$path" ] || continue
        mode=$(stat -c '%a' -- "$path")
        if [ "$mode" != "600" ]; then
            record "overlay:mode:$rel" FAIL "expected mode 600, got $mode"
            all_ok=0
        else
            record "overlay:mode:$rel" PASS "mode 600"
        fi
    done

    # Secrets must never be committed to git.
    local tracked
    tracked=$(git -C "$OUTER_DIR" ls-files -- "$(git -C "$OUTER_DIR" rev-parse --show-prefix 2>/dev/null || true)buildroot-vita/board/vita/local" 2>/dev/null || true)
    local tracked_secret
    tracked_secret=$(printf '%s\n' "$tracked" | grep -vE '(^$|/\.gitignore$|/README\.md$)' || true)
    if [ -n "$tracked_secret" ]; then
        record "overlay:not-committed" FAIL "secret overlay files are tracked by git: $(printf '%s' "$tracked_secret" | tr '\n' ';')"
        all_ok=0
    else
        record "overlay:not-committed" PASS "no overlay secrets are tracked by git"
    fi

    if [ "$all_ok" -eq 1 ]; then
        fact_set bool true overlay.populated
    else
        fact_set bool false overlay.populated
    fi
}
overlay_check

# ------------------------------------------------- 3. zImage / config gate
ZIMAGE="$KERNEL_DIR/arch/arm/boot/zImage"
CONFIG_FILE="$KERNEL_DIR/.config"
zimage_gate() {
    if [ ! -f "$ZIMAGE" ]; then
        record "zimage:present" FAIL "missing: $ZIMAGE"
        return
    fi
    local size sha
    size=$(size_of "$ZIMAGE")
    sha=$(sha256_of "$ZIMAGE")
    fact_set int "$size" artifacts.zImage.size_bytes
    fact_set str "$sha" artifacts.zImage.sha256
    fact_set str "$ZIMAGE" artifacts.zImage.path
    record "zimage:present" PASS "$sha ${size}B"

    if [ ! -f "$CONFIG_FILE" ]; then
        record "zimage:config-initramfs-source" FAIL "no resolved .config at $CONFIG_FILE"
    else
        local src_line src
        src_line=$(grep -m1 '^CONFIG_INITRAMFS_SOURCE=' "$CONFIG_FILE" || true)
        src=$(printf '%s' "$src_line" | sed -n 's/^CONFIG_INITRAMFS_SOURCE="\(.*\)"$/\1/p')
        fact_set str "$src" config.CONFIG_INITRAMFS_SOURCE
        if [ -z "$src" ]; then
            record "zimage:config-initramfs-source" FAIL "CONFIG_INITRAMFS_SOURCE is unset/empty in $CONFIG_FILE — rootfs-less zImage (kernel panic at boot)"
        else
            record "zimage:config-initramfs-source" PASS "CONFIG_INITRAMFS_SOURCE=\"$src\""
        fi
    fi

    if [ "$size" -lt "$MIN_ZIMAGE_BYTES" ]; then
        record "zimage:min-size" FAIL "zImage is ${size}B, below the ${MIN_ZIMAGE_BYTES}B deployable floor — a ~4.2MB image has no embedded rootfs"
    else
        record "zimage:min-size" PASS "${size}B >= ${MIN_ZIMAGE_BYTES}B floor"
    fi
}
zimage_gate

# ------------------------------------------------------------- 4. payload
if [ -n "$PAYLOAD" ]; then
    if [ -f "$PAYLOAD" ]; then
        fact_set str "$(sha256_of "$PAYLOAD")" artifacts.payload.sha256
        fact_set int "$(size_of "$PAYLOAD")" artifacts.payload.size_bytes
        fact_set str "$PAYLOAD" artifacts.payload.path
        record "payload:present" PASS "$PAYLOAD"
    else
        record "payload:present" FAIL "missing: $PAYLOAD"
    fi
fi

# ------------------------------------------------------------- 5. kplugin
if [ -n "$KPLUGIN" ]; then
    if [ -f "$KPLUGIN" ]; then
        fact_set str "$(sha256_of "$KPLUGIN")" artifacts.kplugin.sha256
        fact_set int "$(size_of "$KPLUGIN")" artifacts.kplugin.size_bytes
        fact_set str "$KPLUGIN" artifacts.kplugin.path
        record "kplugin:present" PASS "$KPLUGIN"
    else
        record "kplugin:present" FAIL "missing: $KPLUGIN"
    fi
fi

# ----------------------------------------------------------------- 6. DTBs
DTB_DIR="$KERNEL_DIR/arch/arm/boot/dts"
if [ "$MODELS" = "all" ]; then
    MODELS="vita1000,vita2000,pstv"
fi

get_staged_dtb() {
    local model="$1" entry mfile mpath
    for entry in "${STAGED_DTBS[@]+"${STAGED_DTBS[@]}"}"; do
        mfile="${entry%%=*}"
        mpath="${entry#*=}"
        if [ "$mfile" = "$model" ]; then
            printf '%s' "$mpath"
            return 0
        fi
    done
    return 1
}

IFS=',' read -r -a MODEL_LIST <<< "$MODELS"
for model in "${MODEL_LIST[@]}"; do
    dtb="$DTB_DIR/$model.dtb"
    if [ ! -f "$dtb" ]; then
        record "dtb:$model:present" FAIL "missing build output: $dtb"
        continue
    fi
    dtb_sha=$(sha256_of "$dtb")
    dtb_size=$(size_of "$dtb")
    fact_set str "$dtb_sha" "artifacts.dtb.$model.sha256"
    fact_set int "$dtb_size" "artifacts.dtb.$model.size_bytes"
    fact_set str "$dtb" "artifacts.dtb.$model.path"
    record "dtb:$model:present" PASS "$dtb_sha ${dtb_size}B"

    # Decompile gate: catches truncated/corrupt DTBs regardless of whether a
    # staged copy is supplied for byte-comparison.
    if dtc -q -I dtb -O dts -o "$WORKDIR/$model.dts" -- "$dtb" 2>"$WORKDIR/$model.dtc.err"; then
        record "dtb:$model:decompiles" PASS "dtc decompiled $model.dtb cleanly"
    else
        record "dtb:$model:decompiles" FAIL "dtc failed on $dtb: $(tr '\n' ';' < "$WORKDIR/$model.dtc.err")"
    fi

    if staged=$(get_staged_dtb "$model"); then
        if [ ! -f "$staged" ]; then
            record "dtb:$model:matches-build-output" FAIL "staged DTB missing: $staged"
            continue
        fi
        staged_sha=$(sha256_of "$staged")
        fact_set str "$staged_sha" "artifacts.dtb.$model.staged_sha256"
        fact_set str "$staged" "artifacts.dtb.$model.staged_path"
        if [ "$staged_sha" = "$dtb_sha" ]; then
            record "dtb:$model:matches-build-output" PASS "staged DTB byte-identical to build output ($dtb_sha)"
        else
            record "dtb:$model:matches-build-output" FAIL "staged DTB $staged ($staged_sha) does NOT match build output $dtb ($dtb_sha)"
        fi
    fi
done

# ---------------------------------------------------- 7. embedded initramfs
VMLINUX="$KERNEL_DIR/vmlinux"
if [ -z "$ROOTFS_SOURCE" ] && [ -f "$KERNEL_DIR/rootfs.cpio.zst" ]; then
    ROOTFS_SOURCE="$KERNEL_DIR/rootfs.cpio.zst"
fi

EXTRACTED_ROOTFS=""
if [ ! -f "$VMLINUX" ]; then
    record "initramfs:extract" FAIL "no vmlinux at $VMLINUX — cannot verify embedded initramfs identity"
else
    EXTRACTED_ROOTFS="$WORKDIR/embedded-rootfs.cpio.zst"
    if extract_json=$(python3 "$EXTRACT_PY" --vmlinux "$VMLINUX" --out "$EXTRACTED_ROOTFS" \
            --nm "$NM_BIN" --readelf "$READELF_BIN" 2>"$WORKDIR/extract.err"); then
        fact_set json "$extract_json" initramfs
        emb_sha=$(printf '%s' "$extract_json" | python3 -c 'import json,sys; print(json.load(sys.stdin)["sha256"])')
        emb_size=$(printf '%s' "$extract_json" | python3 -c 'import json,sys; print(json.load(sys.stdin)["size_bytes"])')
        record "initramfs:extract" PASS "extracted via __initramfs_start/.init.data section offset: $emb_sha ${emb_size}B"

        expect="$EXPECT_ROOTFS_SHA256"
        if [ -z "$expect" ] && [ -n "$ROOTFS_SOURCE" ] && [ -f "$ROOTFS_SOURCE" ]; then
            expect=$(sha256_of "$ROOTFS_SOURCE")
            fact_set str "$ROOTFS_SOURCE" rootfs_source.path
            fact_set str "$expect" rootfs_source.sha256
        fi
        if [ -n "$expect" ]; then
            if [ "$expect" = "$emb_sha" ]; then
                record "initramfs:byte-identical" PASS "embedded initramfs matches expected/source rootfs ($emb_sha)"
            else
                record "initramfs:byte-identical" FAIL "embedded initramfs $emb_sha does NOT match expected/source rootfs $expect"
            fi
        else
            record "initramfs:byte-identical" SKIP "no --rootfs-source / --expect-rootfs-sha256 given and no rootfs.cpio.zst found next to the kernel tree"
        fi
    else
        record "initramfs:extract" FAIL "extraction failed: $(tr '\n' ';' < "$WORKDIR/extract.err")"
    fi
fi

# --------------------------------------------- 8. dev/console character dev
console_check() {
    local cpio_src=""
    if [ -n "$EXTRACTED_ROOTFS" ] && [ -f "$EXTRACTED_ROOTFS" ]; then
        cpio_src="$EXTRACTED_ROOTFS"
    elif [ -n "$ROOTFS_SOURCE" ] && [ -f "$ROOTFS_SOURCE" ]; then
        cpio_src="$ROOTFS_SOURCE"
    fi
    if [ -z "$cpio_src" ]; then
        record "rootfs:dev-console" SKIP "no extracted/source rootfs archive available to inspect"
        return
    fi
    local listing
    if ! listing=$(zstd -dc -- "$cpio_src" 2>"$WORKDIR/zstd.err" | cpio -itv 2>"$WORKDIR/cpio.err"); then
        record "rootfs:dev-console" FAIL "could not list rootfs archive $cpio_src: $(tr '\n' ';' < "$WORKDIR/cpio.err")$(tr '\n' ';' < "$WORKDIR/zstd.err")"
        return
    fi
    local line
    line=$(printf '%s\n' "$listing" | awk '$NF == "dev/console" || $NF ~ /\/dev\/console$/ { print; found=1 } END { exit !found }' || true)
    if [ -z "$line" ]; then
        record "rootfs:dev-console" FAIL "dev/console not found in rootfs archive $cpio_src"
        return
    fi
    fact_set str "$line" rootfs.dev_console.raw_listing
    # cpio -tv format: "crw--w--w-   1 root  root  5,   1 <date> dev/console"
    local perm majmin
    perm=$(printf '%s' "$line" | awk '{print $1}')
    majmin=$(printf '%s' "$line" | { grep -oE '[0-9]+,[[:space:]]*[0-9]+' || true; } | head -1 | tr -d ' ')
    fact_set str "$perm" rootfs.dev_console.mode
    fact_set str "$majmin" rootfs.dev_console.major_minor
    if [ "${perm:0:1}" = "c" ] && [ "$majmin" = "5,1" ]; then
        record "rootfs:dev-console" PASS "dev/console is character device 5:1 ($perm)"
    else
        record "rootfs:dev-console" FAIL "dev/console is NOT character device 5:1 (mode=$perm major:minor=$majmin) — init will panic (\"Attempted to kill init!\")"
    fi
}
console_check

# ------------------------------------------------------- 9. remote readback
#
# Offline mode (--readback-dir DIR): compares local build-output hashes
# against files already pulled back by a separate process — no network
# access performed by this tool.
#
# Live mode (--remote-host HOST --remote-readback-live): actually opens an
# SSH/FTP session to the target and pulls the deployed zImage/DTB back for
# byte comparison. IMPLEMENTED, GATED BEHIND TWO EXPLICIT FLAGS, AND NEVER
# INVOKED BY ANY TEST RUN IN THIS REPOSITORY'S HISTORY — see c3-report.md.
# Governing hard constraint for this task: do not ssh/ping/nc real hardware.
remote_readback_live() {
    local host="$1" user="$2" local_zimage="$3" local_dtb="$4" remote_zimage="$5" remote_dtb="$6"
    local remote_zimage_sha remote_dtb_sha
    if ! remote_zimage_sha=$(ssh -o BatchMode=yes -o ConnectTimeout=5 "$user@$host" \
            "sha256sum '$remote_zimage'" 2>"$WORKDIR/ssh.err" | awk '{print $1}'); then
        record "remote-readback" FAIL "live SSH readback of zImage from $host failed: $(tr '\n' ';' < "$WORKDIR/ssh.err")"
        return
    fi
    if ! remote_dtb_sha=$(ssh -o BatchMode=yes -o ConnectTimeout=5 "$user@$host" \
            "sha256sum '$remote_dtb'" 2>"$WORKDIR/ssh.err" | awk '{print $1}'); then
        record "remote-readback" FAIL "live SSH readback of DTB from $host failed: $(tr '\n' ';' < "$WORKDIR/ssh.err")"
        return
    fi
    local local_zimage_sha local_dtb_sha
    local_zimage_sha=$(sha256_of "$local_zimage")
    local_dtb_sha=$(sha256_of "$local_dtb")
    fact_set str "$remote_zimage_sha" remote_readback.live.zimage_sha256
    fact_set str "$remote_dtb_sha" remote_readback.live.dtb_sha256
    if [ "$remote_zimage_sha" = "$local_zimage_sha" ] && [ "$remote_dtb_sha" = "$local_dtb_sha" ]; then
        record "remote-readback" PASS "live readback from $host matches local build output"
    else
        record "remote-readback" FAIL "live readback from $host MISMATCHES local build output (zImage local=$local_zimage_sha remote=$remote_zimage_sha; dtb local=$local_dtb_sha remote=$remote_dtb_sha)"
    fi
}

remote_readback_offline() {
    local dir="$1"
    local primary_model
    primary_model=$(printf '%s' "$MODELS" | cut -d, -f1)
    local expect_zimage="$ZIMAGE"
    local expect_dtb="$DTB_DIR/$primary_model.dtb"
    local rz="$dir/zImage" rd="$dir/$primary_model.dtb"
    [ -f "$rd" ] || rd="$dir/pstv.dtb"
    local ok=1 detail=""
    if [ ! -f "$rz" ]; then
        detail="$detail missing $rz;"
        ok=0
    elif [ ! -f "$expect_zimage" ]; then
        detail="$detail no local zImage to compare against;"
        ok=0
    elif [ "$(sha256_of "$rz")" != "$(sha256_of "$expect_zimage")" ]; then
        detail="$detail zImage mismatch: readback=$(sha256_of "$rz") local=$(sha256_of "$expect_zimage");"
        ok=0
    fi
    if [ ! -f "$rd" ]; then
        detail="$detail missing $rd;"
        ok=0
    elif [ ! -f "$expect_dtb" ]; then
        detail="$detail no local $primary_model.dtb to compare against;"
        ok=0
    elif [ "$(sha256_of "$rd")" != "$(sha256_of "$expect_dtb")" ]; then
        detail="$detail DTB mismatch: readback=$(sha256_of "$rd") local=$(sha256_of "$expect_dtb");"
        ok=0
    fi
    fact_set str "$dir" remote_readback.offline.source_dir
    if [ "$ok" -eq 1 ]; then
        record "remote-readback" PASS "offline readback dir $dir byte-matches local build output"
    else
        record "remote-readback" FAIL "offline readback dir $dir does not match local build output:$detail"
    fi
}

if [ -n "$READBACK_DIR" ]; then
    remote_readback_offline "$READBACK_DIR"
elif [ "$REMOTE_READBACK_LIVE" -eq 1 ] && [ -n "$REMOTE_HOST" ]; then
    if [ -z "$REMOTE_DTB_PATH" ]; then
        record "remote-readback" FAIL "--remote-readback-live requires --remote-dtb-path"
    else
        primary_model=$(printf '%s' "$MODELS" | cut -d, -f1)
        remote_readback_live "$REMOTE_HOST" "$REMOTE_SSH_USER" "$ZIMAGE" \
            "$DTB_DIR/$primary_model.dtb" "$REMOTE_ZIMAGE_PATH" "$REMOTE_DTB_PATH"
    fi
else
    record "remote-readback" SKIP "no --readback-dir and no (--remote-host + --remote-readback-live); hardware contact is out of scope for this run"
fi

# ------------------------------------------- 10. target/firmware + boot log
[ -n "$TARGET_MODEL" ] && fact_set str "$TARGET_MODEL" target.model
[ -n "$TARGET_FIRMWARE" ] && fact_set str "$TARGET_FIRMWARE" target.firmware

if [ -n "$BOOT_LOG" ]; then
    if [ -f "$BOOT_LOG" ]; then
        # Offline parse of an already-captured log: pull ISO-ish timestamps
        # and known port-transition markers (1337/1338/22, ping, ssh, mlan0,
        # hci0, reboot). This never contacts hardware — it only reads a file.
        events_json=$(python3 - "$BOOT_LOG" <<'PY'
import json, re, sys
path = sys.argv[1]
ts_re = re.compile(r'\b(\d{4}-\d{2}-\d{2}T[\d:.+-]+|\d{2}:\d{2}:\d{2})\b')
marker_re = re.compile(r'\b(1337|1338|port 22|ssh|mlan0|hci0|reboot|VitaOS|Linux)\b', re.I)
events = []
with open(path, errors="replace") as f:
    for line in f:
        ts = ts_re.search(line)
        markers = sorted(set(m.group(0) for m in marker_re.finditer(line)))
        if ts and markers:
            events.append({"timestamp": ts.group(0), "markers": markers, "line": line.rstrip("\n")})
print(json.dumps(events))
PY
)
        fact_set json "$events_json" boot_log.events
        fact_set str "$BOOT_LOG" boot_log.source
        record "boot-log:parsed" PASS "$(printf '%s' "$events_json" | python3 -c 'import json,sys;print(len(json.load(sys.stdin)))') timestamped port/state transitions found in $BOOT_LOG"
    else
        record "boot-log:parsed" FAIL "missing: $BOOT_LOG"
    fi
fi

# ---------------------------------------------------------------- render
fact_set str "$(date -u +%Y-%m-%dT%H:%M:%SZ)" generated_at
fact_set str "1.0.0" tool_version

MANIFEST_JSON=$(python3 "$RENDER_PY" < "$FACTS_FILE")
if [ -n "$OUT_JSON" ]; then
    printf '%s\n' "$MANIFEST_JSON" > "$OUT_JSON"
else
    printf '%s\n' "$MANIFEST_JSON"
fi

# ------------------------------------------------------------- summary/exit
{
    printf '\n== vita-manifest summary ==\n'
    fail_count=0
    for i in "${!RESULT_NAMES[@]}"; do
        printf '[%s] %s — %s\n' "${RESULT_STATUS[$i]}" "${RESULT_NAMES[$i]}" "${RESULT_DETAIL[$i]}"
        if [ "${RESULT_STATUS[$i]}" = FAIL ]; then
            fail_count=$((fail_count + 1))
        fi
    done
    printf '%d check(s) failed.\n' "$fail_count"
} >&2

fail_count=0
for status in "${RESULT_STATUS[@]}"; do
    [ "$status" = FAIL ] && fail_count=$((fail_count + 1))
done

if [ "$fail_count" -gt 0 ]; then
    exit 1
fi
exit 0
