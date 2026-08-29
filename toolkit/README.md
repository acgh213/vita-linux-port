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

## Sixth command: `vita-fbserve`

`vita-fbserve` is a read-only HTTP snapshot server for the PSTV framebuffer. It
samples `/dev/fb0`, reduces the fixed 1280×720 RGBA frame to a 320×180 24-bit
BMP, and serves it without changing framebuffer bytes or fbcon ownership:

```sh
# Local-only by default; visit http://127.0.0.1:8080/
vita-fbserve --machine

# Deliberately expose it on the LAN when wanted
vita-fbserve --bind 0.0.0.0 --port 8080
```

`/` and `/frame.bmp` return the current snapshot. `/health` returns a small
read-only status response. Only `GET` is accepted; unsupported methods receive
`405`. The server uses fixed-size buffers, bounded request parsing, and opens
the framebuffer read-only. The installed wrapper launches the static ARM core
from `libexec/vita-fbserve.arm`; the C source is included in `share/` for
reproducible rebuilds. Binding to localhost is the safe default—LAN exposure
requires the explicit `--bind 0.0.0.0` choice.

## Seventh command: `vita-bench`

The benchmark is a small static ARM C program built with the host's hard-float
ARM GCC toolchain. Its source is included in the payload for reproducible
rebuilds, while the deployed binary avoids depending on the native TinyCC.
The native TinyCC remains available for small on-device C experiments; this
particular benchmark uses the verified static GCC artifact because TinyCC's
output for the multi-threaded workload was not stable on the target.

It measures dependency-chain compute, private-buffer `memcpy`, and
private-buffer `memset` across one, two, and four workers (or a selected worker
count):

```sh
vita-bench --machine --quick
vita-bench --machine --workers 4
```

`--quick` is a short smoke run; the default uses larger buffers and more
rounds. Results are machine-readable `key=value` lines and include the online
CPU range, buffer size, worker count, and MiB/s or Mops/s rates. The benchmark
does not open `/dev/fb0` by default and reports `framebuffer=skipped`. The
optional `--framebuffer` path performs one explicit full-frame write for
measuring the display path; use it only when HDMI output changes are wanted.

The workload is intentionally a measurement tool, not a cross-platform score:
compare runs on the same device and record the governor, kernel, and payload
hash with the result.

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

## Control command: `vita-control`

`vita-control` is the first deliberately mutating toolkit command, but it is
local-only: it talks to the already-running demo cart through its PID file and
existing `SIGUSR1` scene-advance contract. It validates that `/proc/$pid/exe`
is the expected cart before sending anything:

```sh
vita-control status --machine
vita-control next --machine
```

`status` reports the cart PID, executable, running state, expected executable,
and configured signal. `next` sends one `SIGUSR1`; accepted requests are
rate-limited to 500 ms across invocations using a locked state file in `/run`.
The action fails closed for a stale PID, a dead process, or an executable
mismatch. It does not expose a network listener. A future HTTP control layer
must add authentication and preserve this validation/rate-limit policy before
LAN mutation is enabled.

## USB inventory command: `vita-usbinfo`

`vita-usbinfo` reads USB device nodes from sysfs and reports topology, bus and
device numbers, VID/PID, device class, negotiated speed, driver, manufacturer,
product, runtime power state, block-device mappings, and mountpoints:

```sh
vita-usbinfo --machine
```

It includes USB root hubs and actual device nodes but skips interface entries.
The output declares `usb_read_only=1`; the command does not write sysfs, touch
VBUS, reset a controller, load a module, mount storage, or access partition
contents. Missing or unavailable attributes are reported as `UNKNOWN` or
`NONE` rather than inferred.

## Input inventory command: `vita-inputinfo`

`vita-inputinfo` reports Linux input event devices from sysfs without opening
the event nodes. It is intentionally identity-first because event numbering
differs between targets: the Vita 1000 currently exposes touchscreen as
`event0` and buttons as `event1`, while the PSTV exposes buttons as `event0`.

```sh
vita-inputinfo --machine
```

Machine output includes the event node, device path, name, `phys` identity, and
the kernel's `ev`, `key`, and `abs` capability bitmaps. The command declares
`input_read_only=1`; it does not consume events, change input state, or assume
that a particular event number represents buttons or touch. The later input
watcher will rank devices by identity and capabilities before reading events.

## Input watcher command: `vita-inputwatch`

`vita-inputwatch` selects an input event node by its `phys` identity, opens it
read-only and nonblocking, and reports bounded evdev records. It defaults to
the Vita button identity and has an explicit time bound; `--max-events` gives
tests and scripts a second deterministic stop condition:

```sh
vita-inputwatch --machine --phys vita_syscon_buttons \
  --duration-ms 5000 --max-events 100
```

The static ARM watcher reports selected-device metadata, raw type/code/value
records, key press/release/repeat counts, absolute-axis count, SYN_REPORT
count, and `status=complete` or `status=timeout`. It never writes an input
device, changes sysfs, controls the cart, or assumes event numbering. The
default target is buttons; touch observation is an explicit future invocation
using the discovered touchscreen identity.

## Native development runner: `vita-dev`

`vita-dev` makes the bundled ARM/TinyCC loop explicit and repeatable. `info`
reports the active toolchain; `build` compiles one C source into an explicitly
named work directory; `run` executes the resulting program and preserves its
exit code:

```sh
vita-dev info --machine
vita-dev build --machine --workdir /tmp/my-test hello.c
vita-dev run /tmp/my-test/hello
```

Build outputs must remain inside `--workdir`, and compiler temporary files are
placed there through `TMPDIR`. The runner supports the payload's normal dynamic
linking contract and does not pretend that `cc -static` works with this TinyCC
and glibc combination. It is a local command, not a service, and never writes
to the read-only payload itself.

`test` compiles the same kind of local project, runs it, compares its exit
status with an explicit expectation, and retains combined program output in the
workdir:

```sh
vita-dev test --machine --workdir /tmp/my-test \
  --expect-status 0 main.c helper.c
```

A passing test reports `status=pass`; an assertion mismatch reports
`status=fail` and exits nonzero. Both records include the expected and observed
statuses plus `stdout_file` for the captured output.

A project manifest keeps this loop ergonomic without becoming a shell script:

```text
output=demo
source=src/main.c
source=src/helper.c
expect_status=0
```

Use it with `vita-dev project --manifest vita.project` or
`vita-dev test --manifest vita.project`. Relative source paths are resolved
from the manifest directory. Only the documented keys are accepted; unknown
keys and duplicate singleton keys fail closed.

## Planned sequence

1. `vita-diag` — machine identity and baseline evidence. **Done.**
2. `vita-netdiag` — interface state and bounded network diagnostics. **Done.**
3. `vita-toolkit-mount` — explicit read-only SquashFS payload mounting. **Done.**
4. `vita-storage` — removable storage inventory without writes. **Done.**
5. Native C toolchain — pinned ARM hard-float TinyCC + matched glibc 2.41
   development sysroot in the external payload. **Done.**
6. `vita-fb` — framebuffer metadata, safe capture/restore, and fbcon ownership
   checks. **Done.**
7. `vita-fbserve` — read-only localhost framebuffer snapshot server. **Done.**
8. `vita-bench` — compute, memory, framebuffer, and worker-scaling measurements.
   **Done.**
9. `vita-control` — local status and explicitly rate-limited scene advance.
   **Done.** Network mutation remains intentionally unimplemented pending
   authentication design.
10. `vita-usbinfo` — read-only USB topology, device, and storage mapping.
    **Done.**
11. `vita-dev` — explicit native compile/run loop in a work directory.
    **Done.**
12. `vita-dev project` — multi-file compile, object link, and local test loop.
    **Done.**
13. `vita-dev test` — compile, run, and assert a project exit status.
    **Done.**
14. `vita-dev` manifests — declarative source lists and test expectations.
    **Done.**
15. `vita-inputinfo` — read-only input event identity and capability inventory.
    **Done.** Hardware-validated on both the 3.65 Vita 1000 and 3.60 PSTV.
16. `vita-inputwatch` — bounded, identity-selected raw input observation.
    **In progress.**

The toolkit's safety rule is simple: diagnostics are read-only by default;
network tests are bounded; framebuffer writes require an explicit subcommand;
raw partition access never happens implicitly.
