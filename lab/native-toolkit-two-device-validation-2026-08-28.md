# Native toolkit — two-device persistent staging validation

Date: 2026-08-28

## Shared payload

B13 native toolkit image:

```text
path: vita-toolkit-native-b13.squashfs
sha256: 74b46fcda36dc6de38d717b2110e3a477750c861987ef9057b857e5c7f83ccf4
bytes: 10604544
version: 0.9.0
```

The same image was staged on both devices and read back byte-for-byte.

## Vita 1000 — firmware 3.65

Device: `192.168.18.36`

Persistent path:

```text
ur0:/linux-toolkit/vita-toolkit-native-b13.squashfs
/dev/mmcblk0p12
UUID=248D-3AC1
```

The Linux-side storage gate mounted the eMMC user partition read-only:

```text
/dev/mmcblk0p12 /mnt/ur0 exfat ro,nosuid,nodev
```

The payload then mounted read-only from that file:

```text
/dev/loop0 /opt/vita-toolkit squashfs ro,nosuid,nodev
```

The B13 manifest test passed on the handheld:

```text
schema=1
action=test
status=pass
source_count=2
expected_status=7
run_status=7
```

Postflight: CPUs `0-3`, clean fault scan, and zero `/mnt/ur0` or
`/opt/vita-toolkit` mounts after cleanup.

## PSTV — firmware 3.60

Device: `192.168.18.43`

Persistent path:

```text
/dev/sdb1:/vita-toolkit-native-b13.squashfs
LABEL="New Volume"
UUID=806A-41E2
```

The removable exFAT partition was discovered dynamically and mounted
read-only for the runtime gate:

```text
/dev/sdb1 /mnt/vita-storage exfat ro,nosuid,nodev
```

The payload mounted read-only:

```text
/dev/loop0 /opt/vita-toolkit squashfs ro,nosuid,nodev
```

The same B13 manifest test passed:

```text
schema=1
action=test
status=pass
source_count=2
expected_status=7
run_status=7
```

Postflight: CPUs stayed `0-3`; cart PID `28289` remained
`/usr/local/bin/pstv-demo-cart`; `fbcon=0`; fault scan stayed clean; zero
storage/toolkit mounts remained after cleanup.

## Result

**PASS.** B13 is now persistently staged and hardware-validated on both
firmware environments. Runtime mounting remains read-only on both the storage
partition and the SquashFS payload. The boot adapters remain distinct:

- Vita 3.65: official `baremetal-loader_363.skprx`
- PSTV 3.60: pre-3.63 loader variant

The next development increment is read-only input discovery/watch tooling,
starting with device identity and capability observation before any cart input
control is enabled.
