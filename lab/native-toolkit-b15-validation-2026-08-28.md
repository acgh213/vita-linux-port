# B15 persistent toolkit validation — 2026-08-28

Image:

```text
path: dist/vita-toolkit-native-b15.squashfs
sha256: 7467e6e0e2b38dc1265622ec1caf0bf4e54f4b55bc197771464a82ed6bf22807
bytes: 10825728
```

The image contains the B15 static ARM `vita-inputwatch` payload and was
validated locally with `unsquashfs -l` before deployment.

## Vita 1000 — firmware 3.65

The image was staged at `ur0:/linux-toolkit/` and then validated through the
Linux-side mount, not merely through the VitaOS transfer path:

```text
/dev/mmcblk0p12 /mnt/ur0 exfat ro,nosuid,nodev
/dev/loop0 /opt/vita-toolkit squashfs ro,nosuid,nodev
image_sha256=7467e6e0e2b38dc1265622ec1caf0bf4e54f4b55bc197771464a82ed6bf22807
image_bytes=10825728
```

Running from `/opt/vita-toolkit`:

```text
vita-inputinfo: input_count=2
event0=PlayStation Vita Touchscreen (Syscon)
event1=PlayStation Vita Buttons (Syscon)
vita-inputwatch: selected_event=event1
vita-inputwatch: selected_phys=vita_syscon_buttons
status=timeout
events_seen=0
watch_exit=1
```

The one-second timeout with no pressed button is expected. Postflight remained
`cpu_online=0-3`, `faults=0`, and `post_cleanup_mounts=0`.

## PSTV — firmware 3.60

The image was staged on the USB exFAT store and validated with read-only
mounts:

```text
/dev/sdb1 /mnt/vita-storage exfat ro,nosuid,nodev
/dev/loop0 /opt/vita-toolkit squashfs ro,nosuid,nodev
image_sha256=7467e6e0e2b38dc1265622ec1caf0bf4e54f4b55bc197771464a82ed6bf22807
image_bytes=10825728
```

Running from `/opt/vita-toolkit`:

```text
vita-inputinfo: input_count=1
event0=PlayStation Vita Buttons (Syscon)
vita-inputwatch: selected_event=event0
vita-inputwatch: selected_phys=vita_syscon_buttons
status=timeout
events_seen=0
watch_exit=1
```

Corrected postflight:

```text
cart_exe=/usr/local/bin/pstv-demo-cart
cpu_online=0-3
faults=0
post_cleanup_mounts=0
```

## Result

**PASS.** B15 is persistently staged and read-only runtime-validated on both
firmware environments. The watcher selects the button device by `phys`, so the
Vita's `event1` and PSTV's `event0` numbering difference is handled without a
platform-specific event-number assumption.
