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
filesystem-image creation; the manifest and dry-run paths remain testable
without it.

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
3. `vita-fb` — framebuffer metadata, safe capture/restore, and scene/cart
   ownership checks.
4. `vita-bench` — NEON/cache/fill-rate measurements, including worker-pool
   scaling once the benchmark contract is settled.
5. `vita-control` — a deliberately small local/network control surface for
   demos, with explicit read-only and mutating modes.

The toolkit's safety rule is simple: diagnostics are read-only by default;
network tests are bounded; framebuffer writes require an explicit subcommand;
raw partition access never happens implicitly.
