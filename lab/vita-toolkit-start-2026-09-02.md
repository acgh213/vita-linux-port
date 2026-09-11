# Vita Linux toolkit start — 2026-09-02

## Scope

First working slice of the toolkit: build the external native-toolchain
SquashFS payload, verify it reproducibly, and smoke-test the read-only
inventory/input commands on the live target at `192.168.18.43`.

This is a userspace-only cycle. No kernel image, DTB, VitaOS configuration,
framebuffer contents, storage partition, or persistent target state was
modified.

## Source and artifacts

- Outer repo tip: `25c884c5a261b0f907bc06e944f1e7c2a785bcf4`
- `vita-inputwatch` source commit: `0d556e3749da1f6530f29969eb588a8d97e0fdaf`
- Toolkit version: `1.1.0`
- TCC revision: `2ba12e83b3599ca8f5d50c179fe5138fe956f0c9`
- Artifact: `dist/vita-toolkit-native-2026-09-02.squashfs`
- Artifact size: `10,825,728` bytes
- Artifact SHA-256: `6fb29caf00b1bf614399b471e35d719dcad4bdf3a00f6a3c0eb66022aed997a2`
- SquashFS: version 4.0, zstd, fixed epoch, one processor, no xattrs

The image contains the toolkit commands, ARM helper binaries, source files,
manifests, pinned ARM TinyCC, and the glibc development sysroot. Required
payload paths were checked after creation, including `bin/tcc`, `bin/cc`,
`bin/vita-diag`, `bin/vita-netdiag`, `bin/vita-inputinfo`,
`bin/vita-inputwatch`, `libexec/vita-inputwatch.arm`,
`sysroot/usr/include/stdio.h`, and `sysroot/usr/lib/libc.so`.

A second build compared byte-for-byte equal to the primary artifact:

```text
reproducible=1
6fb29caf00b1bf614399b471e35d719dcad4bdf3a00f6a3c0eb66022aed997a2  primary
6fb29caf00b1bf614399b471e35d719dcad4bdf3a00f6a3c0eb66022aed997a2  repro
primary_bytes=10825728
repro_bytes=10825728
```

## Host gate

All 17 toolkit contract tests passed, including the input watcher fake-evdev
fixture and SquashFS safe-path tests.

```text
TOOLKIT_TEST_SUMMARY passed=17 failed=0
```

The ARM artifacts were checked with `file`; the watcher, benchmark,
framebuffer server, cart control helper, and native `tcc` all identify as
32-bit ARM EABI5 statically linked executables.

## Live target smoke gate

Target `192.168.18.43` accepted SSH and reported:

```text
kernel=6.12.0-gc477a244a49b
arch=armv7l
cpu_online=0-3
framebuffer=1280x720,32bpp
mlan0=192.168.18.43/24
```

The target's input identity was:

```text
event0_phys=vita_syscon_buttons
event0_name=PlayStation Vita Buttons (Syscon)
```

The following three files were copied to `/tmp` temporarily and verified by
matching host/target SHA-256 before execution:

- `vita-diag`: `aa672744d8ce6c98429f50429096c10ff0e03d94fbb90267810af73c43ef0614`
- `vita-netdiag`: `57156f80a9ca38b242c4f17009ac731a830cc56a7428e6f964fa91f0e9ccdfdf`
- `vita-inputwatch.arm`: `613ee7cd208389d7d1f0ec914e91fde60a9051861a7b4261c76047d930f8d675`

The target outputs confirmed four online CPUs, Wi-Fi route/DNS state, and
successful identity selection/open of the real input device. The bounded
watcher ran for 250 ms with no physical event supplied:

```text
selected_event=event0
selected_device=/dev/input/event0
selected_phys=vita_syscon_buttons
status=timeout
events_seen=0
key_presses=0
key_releases=0
key_repeats=0
abs_events=0
syn_reports=0
watch_exit=1
```

`status=timeout` and exit 1 are the documented result when the time bound
expires before an event arrives; this confirms the bounded stop path, not a
crash. The temporary target directory was removed and cleanup was verified.

## Integration status and correction

This target was not a fresh toolkit-capable image. It was the later known-good
rollback baseline `6.12.0-gc477a244a49b`, which intentionally predates the
SquashFS kernel commit. Its `/proc/filesystems` correctly lacked `squashfs`,
and it had no `/lib/modules` tree from which a module could be loaded.

SquashFS support is already hardware-validated. The 2026-08-25 USB payload
record proves that the target previously ran `6.12.0-ga49f3db6a94f` with
`CONFIG_SQUASHFS=y` and `CONFIG_SQUASHFS_ZSTD=y`, mounted the payload through
`vita-toolkit-mount`, executed toolkit commands from the mount, and cleaned up
successfully. Later GPU/clock gates restored `gc477a` as the safe display
baseline; the toolkit payload and newer command work then continued in the
repository without being re-deployed onto this rollback image.

The earlier wording in this record—"next integration must ensure SquashFS
support is available"—was therefore wrong. The actual next step is to use the
existing SquashFS-enabled kernel/rootfs lineage, or deliberately rebuild a
new candidate from that lineage, then deploy the new payload with the
rollback-first procedure. Do not interpret the current baseline's missing
registration as a regression. Do not infer input event counts without an
actual button/touch event.

## Next step

Reconcile the toolkit source/worktree with the known SquashFS-enabled kernel
line, build a deliberate candidate, and repeat the already-proven USB mount
cycle using the new native payload. Keep the current `gc477a` image as the
rollback pair; do not replace it casually.
