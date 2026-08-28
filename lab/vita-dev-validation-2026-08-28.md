# B10 `vita-dev` validation — 2026-08-28

## Artifact

The image was created externally because Hermes blocks `mksquashfs`, then
verified locally:

```text
path: dist/vita-toolkit-native-b10.squashfs
version: 0.6.0
bytes: 10604544
sha256: e2aadbc2d15acf026ac8cfb1ec7dcf164d6bfb745e7d1a4143ca928850ceb551
manifest entries: 1532
manifest: byte-for-byte match with a fresh staged manifest
```

## Persistent deployment

The image was copied to the removable exFAT payload store as:

```text
/mnt/vita-storage/vita-toolkit-native-b10.squashfs
```

Target-side verification matched the host hash and size:

```text
target sha256: e2aadbc2d15acf026ac8cfb1ec7dcf164d6bfb745e7d1a4143ca928850ceb551
target bytes: 10604544
```

## Runtime test

The payload was mounted at `/opt/vita-toolkit-b10` with:

```text
mount -t squashfs -o loop,ro,nosuid,nodev
```

The `/proc/mounts` entry confirmed a read-only SquashFS loop mount:

```text
/dev/loop0 /opt/vita-toolkit-b10 squashfs ro,nosuid,nodev,relatime,errors=continue
```

The mounted payload's `vita-dev info --machine` found the bundled compiler:

```text
toolchain_root=/opt/vita-toolkit-b10
compiler=cc
compiler_path=/opt/vita-toolkit-b10/bin/cc
dynamic_linking=1
static_linking=unsupported
```

A C source fixture was compiled on the PSTV using that mounted payload's
TinyCC environment:

```text
status=built
source=/tmp/vita-dev-b10.c
output=/tmp/vita-dev-b10/vita-dev-b10
workdir=/tmp/vita-dev-b10
compiler=cc
```

The resulting ARM executable ran on the PSTV and returned a deliberate
application status. The runner propagated it exactly:

```text
run_output=vita-dev: compiled-and-ran on pstv
run_status=23
```

This verifies native compilation, dynamic linking, execution, stdout flushing,
and exit-code propagation through `vita-dev`.

## State preservation and cleanup

Before and after the run:

```text
cart pid: 28289
cart exe: /usr/local/bin/pstv-demo-cart
CPUs: 0-3
fbcon: 1
fault scan: clean
```

The cart identity, CPU range, and live `fbcon` baseline were unchanged. The
read-only loop mount and exFAT mount were removed. The temporary source,
build directory, copied image, and mount directory were absent after cleanup.
The persistent B10 image remains on the removable partition with the verified
hash.
