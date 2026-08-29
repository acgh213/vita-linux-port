# B12 `vita-dev test` validation — 2026-08-28

## Artifact

The image was created externally because Hermes blocks `mksquashfs`, then
verified locally:

```text
path: dist/vita-toolkit-native-b12.squashfs
version: 0.8.0
bytes: 10604544
sha256: 4a37d3bbd13a3d69e27b5d789f8bed2bbaae6d848d6756015b1b3abb34bea1ac
manifest entries: 1532
manifest: byte-for-byte match with a fresh staged manifest
```

## Persistent deployment

The image was copied to the removable exFAT payload store as:

```text
/mnt/vita-storage/vita-toolkit-native-b12.squashfs
```

Target-side verification matched the host hash and size:

```text
target sha256: 4a37d3bbd13a3d69e27b5d789f8bed2bbaae6d848d6756015b1b3abb34bea1ac
target bytes: 10604544
```

## Runtime test

The payload was mounted at `/opt/vita-toolkit-b12` with:

```text
mount -t squashfs -o loop,ro,nosuid,nodev
```

The target found the bundled compiler:

```text
compiler_path=/opt/vita-toolkit-b12/bin/cc
dynamic_linking=1
static_linking=unsupported
```

A two-translation-unit project and shared header were compiled, linked, and
asserted on the PSTV. The passing assertion was:

```text
source_count=2
expected_status=7
run_status=7
status=pass
```

The deliberate failing assertion was also verified:

```text
expected_status=0
run_status=7
status=fail
nonzero command exit
```

Program output was retained in each explicit workdir's
`.vita-dev-test-output` file. The tool's machine output remained stable and
reported the output file path rather than embedding arbitrary program output.

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
project directories, copied image, and mount directory were removed after the
run. The persistent B12 image remains on the removable partition with the
verified hash.
