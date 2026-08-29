# B13 manifest-driven `vita-dev` validation — 2026-08-28

## Artifact

The image was created externally because Hermes blocks `mksquashfs`, then
verified locally:

```text
path: dist/vita-toolkit-native-b13.squashfs
version: 0.9.0
bytes: 10604544
sha256: 74b46fcda36dc6de38d717b2110e3a477750c861987ef9057b857e5c7f83ccf4
manifest entries: 1532
manifest: byte-for-byte match with a fresh staged manifest
```

## Persistent deployment

The image was copied to the removable exFAT payload store as:

```text
/mnt/vita-storage/vita-toolkit-native-b13.squashfs
```

Target-side verification matched the host hash and size:

```text
target sha256: 74b46fcda36dc6de38d717b2110e3a477750c861987ef9057b857e5c7f83ccf4
target bytes: 10604544
```

## Runtime test

The payload was mounted at `/opt/vita-toolkit-b13` with:

```text
mount -t squashfs -o loop,ro,nosuid,nodev
```

A manifest outside the payload declared a relative two-source project:

```text
output=manifest-app
source=src/main.c
source=src/status.c
expect_status=7
```

The mounted payload's own compiler was selected:

```text
compiler_path=/opt/vita-toolkit-b13/bin/cc
```

`vita-dev test --machine --manifest` resolved the relative sources, compiled
and linked both translation units, ran the resulting ARM executable, and
asserted its deliberate status:

```text
action=test
status=pass
source_count=2
expected_status=7
run_status=7
stdout_file=/tmp/vita-dev-b13-run/.vita-dev-test-output
```

The captured program output contained:

```text
manifest test value=7
```

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
read-only loop mount and exFAT mount were removed. Temporary source files,
project directories, copied image, and mount directory were removed after the
run. The persistent B13 image remains on the removable partition with the
verified hash.
