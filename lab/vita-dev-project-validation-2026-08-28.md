# B11 `vita-dev project` validation — 2026-08-28

## Artifact

The image was created externally because Hermes blocks `mksquashfs`, then
verified locally:

```text
path: dist/vita-toolkit-native-b11.squashfs
version: 0.7.0
bytes: 10604544
sha256: f0ddb09a7d261f72c2db5bffe71c679206593c54fbdddf582fbe6fb797835b44
manifest entries: 1532
manifest: byte-for-byte match with a fresh staged manifest
```

## Persistent deployment

The image was copied to the removable exFAT payload store as:

```text
/mnt/vita-storage/vita-toolkit-native-b11.squashfs
```

Target-side verification matched the host hash and size:

```text
target sha256: f0ddb09a7d261f72c2db5bffe71c679206593c54fbdddf582fbe6fb797835b44
target bytes: 10604544
```

## Runtime test

The payload was mounted at `/opt/vita-toolkit-b11` with:

```text
mount -t squashfs -o loop,ro,nosuid,nodev
```

The `/proc/mounts` entry confirmed a read-only SquashFS loop mount:

```text
/dev/loop0 /opt/vita-toolkit-b11 squashfs ro,nosuid,nodev,relatime,errors=continue
```

The mounted payload's `vita-dev info --machine` found its bundled compiler:

```text
compiler_path=/opt/vita-toolkit-b11/bin/cc
dynamic_linking=1
static_linking=unsupported
```

Two C translation units plus a shared header were compiled separately and
linked on the PSTV by the mounted payload:

```text
action=project
status=built
source_count=2
output=/tmp/vita-dev-b11-project/app
workdir=/tmp/vita-dev-b11-project
compiler=cc
```

The resulting ARM executable ran on the PSTV and returned a deliberate
application status:

```text
run_output=vita-dev project answer=42
run_status=17
```

The first attempt exposed only a test-fixture naming error: the transferred
header was named `vita-dev-b11-answer.h` while the source included `answer.h`.
Renaming it into the source directory fixed the fixture; no toolkit code change
was needed. The corrected run passed completely.

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
read-only loop mount and exFAT mount were removed. The temporary source files,
project directory, copied image, and mount directory were removed after the
run. The persistent B11 image remains on the removable partition with the
verified hash.
