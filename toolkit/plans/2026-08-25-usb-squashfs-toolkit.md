# USB Squashfs Toolkit Payload Implementation Plan

> **For Hermes:** Use this plan task-by-task with host gates before PSTV action.

**Goal:** Build a reproducible, read-only `vita-toolkit.squashfs` payload that can be mounted from USB after the embedded Vita Linux rescue/initramfs boots.

**Architecture:** Keep the kernel, BusyBox, SSH, networking, and recovery tools embedded in the existing cpio initramfs. Put the expanding toolkit payload on a labeled USB partition and mount it explicitly at `/opt/vita-toolkit` as read-only. Do not make boot depend on USB until this payload path has survived repeated cold-boot and removal tests.

**Tech Stack:** POSIX shell, Buildroot overlay, Linux SquashFS, host `mksquashfs`, BusyBox mount/blkid/lsblk where available, SHA-256 manifests, existing Vita USB host/storage support.

---

## Current boundary

The embedded image already carries `vita-diag`, `vita-netdiag`, and the known-good demo cart. The PSTV has booted the toolkit-bearing zImage and passed network, framebuffer, and fault gates. The USB payload is additive: if the drive is absent, Linux must still boot into the same rescue environment.

The current kernel defconfig has USB host/storage support, but SquashFS support must be explicitly checked and enabled before the device mount test. The plan should prefer SquashFS Zstandard compression if the kernel config supports it; otherwise use the simplest available compression and record it in the manifest.

## Payload contract

```text
USB partition label: VITA_TOOLKIT
Filesystem:         FAT32 or exFAT transport filesystem
Payload file:       /vita-toolkit.squashfs
Mount point:        /opt/vita-toolkit
Mount mode:         read-only
Payload root:
├── MANIFEST
├── VERSION
├── bin/
│   ├── vita-diag
│   ├── vita-netdiag
│   └── (future vita-fb, vita-bench, demos)
└── share/
    └── (documentation and fixture metadata)
```

The first transport should be a normal USB partition containing the SquashFS
file, not a raw SquashFS partition. That gives us ordinary host tooling and a
simple recovery story. A raw partition can be evaluated later if there is a
clear performance or capacity reason.

## Safety rules

- USB absence must never prevent Linux boot.
- No init script may format, repartition, or write to a discovered block device.
- Mount requires an explicit device/path or the exact `VITA_TOOLKIT` label.
- Mount is idempotent and refuses a non-SquashFS payload.
- The payload is mounted `ro`; logs, captures, and mutable state go to `/run` or
  another explicit writable location.
- The embedded commands remain available as fallback while the payload is
  experimental.
- Never copy Wi-Fi credentials, SSH keys, or other local overlay material into
  the SquashFS staging tree.

---

## Phase 1: Kernel capability gate

### Task 1: Record current filesystem support

**Files:**
- Inspect: `linux_vita/arch/arm/configs/vita_defconfig`
- Modify: `toolkit/plans/` only if the observed config differs from this plan

Run:

```sh
grep -E 'CONFIG_(SQUASHFS|USB_STORAGE|BLK_DEV_LOOP)' \
  linux_vita/arch/arm/configs/vita_defconfig
```

Expected: USB storage is enabled; SquashFS may be absent and becomes the next
minimal kernel change.

### Task 2: Enable the minimal SquashFS options

**Files:**
- Modify: `linux_vita/arch/arm/configs/vita_defconfig`

Add only the options required for the first payload test:

```text
CONFIG_SQUASHFS=y
CONFIG_SQUASHFS_ZSTD=y
```

If the kernel version/config rejects Zstandard support, use the smallest
supported compression and record that choice in `BUILDING.md`; do not enable a
large menu of filesystem features speculatively.

### Task 3: Build and inspect the kernel configuration

Run:

```sh
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- \
  -C linux_vita olddefconfig
 grep -E 'CONFIG_(SQUASHFS|SQUASHFS_ZSTD|USB_STORAGE)' linux_vita/.config
```

Expected: all required symbols are `y`.

### Task 4: Compile the host-only kernel artifact

Run:

```sh
cp /path/to/known-good/rootfs.cpio.zst linux_vita/rootfs.cpio.zst
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- \
  -C linux_vita zImage -j4
file linux_vita/arch/arm/boot/zImage
sha256sum linux_vita/arch/arm/boot/zImage
```

Do not deploy until the SquashFS-enabled image passes the remaining host gates.

---

## Phase 2: Reproducible host image builder

### Task 5: Define the source manifest

**Files:**
- Create: `toolkit/squashfs/MANIFEST.in`
- Modify: `toolkit/README.md`

The manifest names only committed toolkit files and their destination paths.
It must exclude `.git`, `buildroot-vita/board/vita/local/`, credentials, kernel
artifacts, temporary captures, and host-specific files.

### Task 6: Write the failing builder test

**Files:**
- Create: `toolkit/tests/test-squashfs-image.sh`

The test should create a temporary staging tree, invoke the builder in dry-run
or test mode, and assert that the output contains `MANIFEST`, `VERSION`, and
the two toolkit commands but no `.ssh`, `wpa_supplicant.conf`, or `local/`
paths.

Run:

```sh
toolkit/tests/test-squashfs-image.sh
```

Expected initially: FAIL because the builder does not exist.

### Task 7: Implement the builder

**Files:**
- Create: `tools/build-vita-toolkit-squashfs.sh`
- Create: `toolkit/squashfs/VERSION`

Required interface:

```sh
tools/build-vita-toolkit-squashfs.sh \
  --output dist/vita-toolkit.squashfs \
  --source toolkit
```

Required behavior:

1. Refuse a missing source or output directory.
2. Stage only approved files into a temporary directory.
3. Write a deterministic manifest containing relative path, mode, size, and
   SHA-256 for every payload file.
4. Use `mksquashfs` with explicit compression and reproducibility flags.
5. Print the output SHA-256, byte size, compression, and file count.
6. Never overwrite an existing output without `--force`.

### Task 8: Run host image tests

Run:

```sh
toolkit/tests/test-vita-diag.sh
toolkit/tests/test-vita-netdiag.sh
toolkit/tests/test-squashfs-image.sh
unsquashfs -ll dist/vita-toolkit.squashfs
sha256sum dist/vita-toolkit.squashfs
```

Expected: all tests pass; only approved toolkit files are listed; repeated
builds from the same source produce the same manifest and, if the host tool
supports it, the same image hash.

### Task 9: Add a host mount verification gate

**Files:**
- Modify: `toolkit/tests/test-squashfs-image.sh`

Mount the image through a loop device in a temporary directory when the host
has the required privilege, verify it is read-only, execute the staged
`vita-diag --machine`, then unmount it. If loop mounting is unavailable, keep
`unsquashfs` verification as the portable gate and report the skipped mount
check explicitly.

---

## Phase 3: Explicit device mount helpers

### Task 10: Write the mount contract test

**Files:**
- Create: `toolkit/tests/test-vita-toolkit-mount.sh`

Test these cases against fake `/dev`, `/proc`, and `/sys` roots:

- explicit image path mounts once
- repeated mount is a no-op
- absent device returns a clear failure without writes
- wrong label/path is rejected
- unmount removes only the toolkit mount

### Task 11: Implement explicit mounting

**Files:**
- Create: `toolkit/bin/vita-toolkit-mount`
- Create: `toolkit/bin/vita-toolkit-unmount`
- Create: `buildroot-vita/board/vita/overlay/usr/local/bin/vita-toolkit-mount`
- Create: `buildroot-vita/board/vita/overlay/usr/local/bin/vita-toolkit-unmount`

Interface:

```sh
vita-toolkit-mount /mnt/usb/vita-toolkit.squashfs
vita-toolkit-mount --label VITA_TOOLKIT /mnt/usb
vita-toolkit-unmount
```

The helper should mount the SquashFS file loop-backed, read-only, at
`/opt/vita-toolkit`, validate `MANIFEST`, and export a short success summary.
It should not be an auto-start init script yet.

### Task 12: Add optional path discovery without auto-mounting

**Files:**
- Modify: `toolkit/bin/vita-toolkit-mount`
- Modify: `toolkit/README.md`

Support an explicit `--label VITA_TOOLKIT` path only after confirming the
available BusyBox/util-linux applets on the target. If discovery is ambiguous,
fail closed and require an explicit path.

---

## Phase 4: Hardware USB payload cycle

### Task 13: Prepare a disposable USB payload

**Files:**
- Host-only artifact: `dist/vita-toolkit.squashfs`
- Host-only artifact: `dist/SHA256SUMS`

Use a disposable USB partition. Do not repartition the drive from the Vita.
Copy the SquashFS file, manifest, and checksum; read them back from the host
before inserting the device.

### Task 14: Verify USB enumeration without mounting

Boot the existing known-good/toolkit-bearing zImage first. Over SSH, capture:

```sh
cat /sys/class/block/*/uevent
ls /dev/sd* /dev/mmcblk* 2>/dev/null
cat /proc/mounts
```

Confirm the USB device and partition without writing to it. Record the exact
node and transport path.

### Task 15: Mount and exercise the payload

Run the explicit mount helper, then verify:

```sh
mount | grep /opt/vita-toolkit
/usr/local/bin/vita-toolkit-mount /mnt/usb/vita-toolkit.squashfs
/opt/vita-toolkit/bin/vita-diag --machine
/opt/vita-toolkit/bin/vita-netdiag --machine
```

Expected: mount is read-only, both commands run from the SquashFS, and the
embedded fallback commands remain available.

### Task 16: Removal and reboot safety test

Unmount, remove the USB device, and cold-boot twice. Expected: Linux reaches
SSH and the embedded diagnostics/cart still work with no USB present. A missing
payload must be an ordinary warning, never a boot failure.

---

## Phase 5: Portable payload expansion

Only after Phase 4 passes:

1. Add `vita-fb` for framebuffer metadata and safe capture/restore.
2. Add `vita-bench` and the first worker-pool/NEON measurements.
3. Add demo binaries and scene manifests to the payload.
4. Add host `tools/pstv-*` deployment/capture helpers.
5. Consider a read-only USB payload auto-discovery service, still without
   making the boot path depend on the device.

The full-rootfs-on-USB / `switch_root` or overlay handoff is a separate later
project. It must not be combined with the first SquashFS payload milestone.

## Acceptance criteria

The milestone is complete when:

- Host builder produces a documented, hashable SquashFS with no credentials.
- Host tests verify file allowlisting, manifest integrity, and image contents.
- Kernel has explicit SquashFS support and still boots the embedded rescue
  initramfs.
- PSTV mounts the payload read-only from USB through an explicit command.
- `vita-diag` and `vita-netdiag` execute from the payload.
- USB removal and cold boot leave the rescue environment functional.
- No automatic formatting, partition writes, or boot dependency has been added.
