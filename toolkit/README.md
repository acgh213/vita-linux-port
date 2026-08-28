# Vita Linux Toolkit

The toolkit is a BusyBox-first, scriptable userspace layer for Vita Linux.
It is deliberately separate from the graphics cart: the cart demonstrates the
framebuffer; the toolkit explains and exercises the machine around it.

## Shape

- `toolkit/bin/vita-*` — on-device commands installed in
  `/usr/local/bin/` by the rootfs overlay.
- `toolkit/tests/` — host-side shell tests using a fake proc/sys/dev root.
- `tools/pstv-*` — host-side deployment, capture, and evidence helpers (next
  layer; not mixed into device commands).
- Output has two modes: human-readable by default, stable `key=value` lines
  with `--machine` for scripts and future network/API consumers.

## First command: `vita-diag`

Read-only inventory of the running Linux instance:

- kernel and architecture
- uptime and memory summary
- online CPU topology
- framebuffer geometry and pixel format
- visible network interfaces and operational state
- stable `/dev/vita/*` eMMC partition links

It does not require Python, Lua, curl, or a writable persistent filesystem.
`VITA_DIAG_ROOT` is a host-test seam; production use leaves it unset.

## Building the payload

The host builder stages only the approved toolkit files and emits a deterministic
manifest before invoking `mksquashfs`:

```sh
tools/build-vita-toolkit-squashfs.sh \
  --source toolkit \
  --output dist/vita-toolkit.squashfs
```

Useful safe inspection modes:

```sh
tools/build-vita-toolkit-squashfs.sh --source toolkit --manifest-only
tools/build-vita-toolkit-squashfs.sh --source toolkit --dry-run
```

The builder refuses overwrite by default, excludes local overlays and
credentials, fixes the image timestamp, uses one compressor worker for
reproducibility, and prints the final SHA-256. The actual `mksquashfs` step is
an explicit host gate in environments whose command safety layer blocks
filesystem-image creation. `--stage-output DIR` atomically publishes the exact
verified root for that external gate; `--manifest-only` and `--dry-run` remain
available without creating an image.

## Native C development

The optional native-toolchain payload turns the running PSTV into a small ARM C
development machine. It contains a pinned ARM hard-float TinyCC, TCC's native
runtime, and a glibc 2.41 development sysroot matching the Buildroot image.
The compiler itself is static; programs are dynamically linked against the
running system's `/lib/ld-linux-armhf.so.3` and `/lib/libc.so.6` by default.

Build the generated toolchain tree from the pinned source checkout:

```sh
git clone "$(cat toolkit/toolchain/TCC_SOURCE_URL)" /tmp/tinycc
TCC_REV=$(cat toolkit/toolchain/TCC_REVISION)
git -C /tmp/tinycc checkout "$TCC_REV"

tools/build-vita-native-toolchain.sh \
  --tcc-source /tmp/tinycc \
  --sysroot /usr/arm-linux-gnueabihf \
  --output dist/vita-native-toolchain

tools/build-vita-toolkit-squashfs.sh \
  --source toolkit \
  --toolchain-root dist/vita-native-toolchain \
  --stage-output dist/vita-toolkit-native-root
```

After mounting the resulting SquashFS at `/opt/vita-toolkit`:

```sh
. /opt/vita-toolkit/toolchain-env.sh
cc hello.c -o hello
./hello
```

`cc` is a relative symlink to `tcc`; `toolchain-env.sh` adds the payload's
`bin/` directory to `PATH` and exports `CC=cc` and `TCC`.

### Verified native capabilities

The production payload has been exercised on the PSTV itself with:

- separate compilation of two C translation units and object linking;
- glibc headers and normal dynamic linking;
- `libm` and four pthread workers;
- executable output using `/lib/ld-linux-armhf.so.3`;
- `cc -run` with return-code propagation;
- expected compiler diagnostics on invalid C.

### Known TinyCC boundaries

- **`cc -static` is not supported.** TinyCC's ARM linker does not understand
  modern relocation types in Debian 13's glibc 2.41 static archives. Dynamic
  output is the supported contract. The archives remain in the sysroot for a
  future GNU binutils/GCC layer, not as a claim that TCC can consume all of
  them.
- **Flush stdio explicitly in `cc -run` programs.** In-process execution
  returns from `main()` without libc's ordinary process-exit flush. Call
  `fflush(stdout)` before returning when output is buffered. Normal compiled
  executables do not have this limitation.
- TinyCC is the fast bootstrap compiler, not an optimizing GCC replacement.
  Performance-sensitive production binaries should still be cross-built with
  `arm-linux-gnueabihf-gcc` on the Debian host.
- The compiler paths are intentionally pinned to `/opt/vita-toolkit`; mount the
  payload there rather than relocating it.


## Mounting the external payload

`vita-toolkit-mount` is installed in the embedded rescue environment so the
rescue layer can mount the USB payload without depending on the payload itself.
The storage partition and payload image are separate explicit steps:

```sh
mount -t exfat -o ro /dev/<storage-partition> /mnt/vita-storage
vita-toolkit-mount --file /mnt/vita-storage/vita-toolkit.squashfs \
  --target /opt/vita-toolkit --machine
vita-toolkit-mount --unmount --target /opt/vita-toolkit --machine
umount /mnt/vita-storage
```

`/dev/<storage-partition>` is deliberately a placeholder: device names can
change with USB enumeration order. Confirm the actual partition first, then
pass that exact path explicitly. Automatic USB discovery is intentionally not
part of this first helper.

Storage is mounted read-only as exFAT; the SquashFS image uses a read-only loop
mount with `nosuid,nodev`. The helper refuses system directories and reports
stable status fields for scripts.

## Third command: `vita-storage`

`vita-storage` inventories removable partitions without mounting, formatting,
partitioning, or writing to them. It identifies partitions through sysfs and
requires the parent disk's `removable` flag; it does not assume `/dev/sda*`,
`/dev/sdb*`, or any other enumeration order. Filesystem metadata comes from
`blkid`, and mount state is read from `/proc/mounts`.

```sh
vita-storage --machine
```

Machine output uses one record per removable partition:

```text
schema=1
storage_count=1
storage_1_device=/dev/sdb1
storage_1_parent=/dev/sdb
storage_1_removable=1
storage_1_filesystem=exfat
storage_1_label=New Volume
storage_1_uuid=806A-41E2
storage_1_mounted=0
storage_1_mountpoint=NONE
```

The displayed device name is informational. Any later mount operation must
still be explicit and separately reviewed.

## Fifth command: `vita-fb`

`vita-fb` makes the simple framebuffer observable without making display writes
implicit. It reads geometry, stride, pixel format, and fbcon ownership through
sysfs:

```sh
vita-fb info --machine
vita-fb capture /tmp/framebuffer.raw
vita-fb restore /tmp/framebuffer.raw
```

Captures are written to a temporary sibling and published only after the exact
`stride * height` byte count is present. Existing captures are never overwritten.
Restore is an explicit operation: a short input is rejected before `/dev/fb0`
is touched, fbcon is unbound only when it was originally bound, and its original
ownership is restored even when the framebuffer write fails. The default PSTV
frame is 1280x720 at 32 bpp with a 5120-byte stride, or 3,686,400 bytes in 900
4-KiB blocks.

The host test uses a fake sysfs and framebuffer root and covers complete and
truncated captures/restores plus bound and unbound fbcon states. It does not
require display output or hardware.

## Second command: `vita-netdiag`

Read-only network inventory using the image's existing `eth0` and `mlan0`
conventions:

- interface operational state
- IPv4 addresses
- default route and gateway interface
- configured nameservers
- optional single bounded ping with explicit `--probe HOST`

Without `--probe`, it does not generate network traffic. It uses stable
`key=value` output with `--machine`, like `vita-diag`.

## Planned sequence

1. `vita-diag` — machine identity and baseline evidence. **Done.**
2. `vita-netdiag` — interface state and bounded network diagnostics. **Done.**
3. `vita-toolkit-mount` — explicit read-only SquashFS payload mounting. **Done.**
4. `vita-storage` — removable storage inventory without writes. **Done.**
5. Native C toolchain — pinned ARM hard-float TinyCC + matched glibc 2.41
   development sysroot in the external payload. **Done.**
6. `vita-fb` — framebuffer metadata, safe capture/restore, and fbcon ownership
   checks. **Done.**
7. `vita-bench` — NEON/cache/fill-rate measurements, including worker-pool
   scaling once the benchmark contract is settled.
8. `vita-control` — a deliberately small local/network control surface for
   demos, with explicit read-only and mutating modes.

The toolkit's safety rule is simple: diagnostics are read-only by default;
network tests are bounded; framebuffer writes require an explicit subcommand;
raw partition access never happens implicitly.
