# `vita-inputinfo` hardware validation — 2026-08-28

B14 introduces `vita-inputinfo`, a read-only sysfs inventory. It reports input
event identity and capability bitmaps without opening event nodes or consuming
input. Host fake-sysfs tests passed before the hardware run.

## Vita 1000 — firmware 3.65

Target: `192.168.18.36`

```text
schema=1
input_read_only=1
input_count=2
input_1_event=event0
input_1_device=/dev/input/event0
input_1_name=PlayStation Vita Touchscreen (Syscon)
input_1_phys=vita_syscon_ts
input_1_ev=b
input_1_key=400 0 0 0 0 0 0 0 0 0 0
input_1_abs=6608000 1000003
input_2_event=event1
input_2_device=/dev/input/event1
input_2_name=PlayStation Vita Buttons (Syscon)
input_2_phys=vita_syscon_buttons
input_2_ev=b
input_2_key=f 0 0 0 0 0 0 0 1cdb0000 0 0 0 0 0 1c0000 0 0
input_2_abs=1b
```

## PSTV — firmware 3.60

Target: `192.168.18.43`

```text
schema=1
input_read_only=1
input_count=1
input_1_event=event0
input_1_device=/dev/input/event0
input_1_name=PlayStation Vita Buttons (Syscon)
input_1_phys=vita_syscon_buttons
input_1_ev=b
input_1_key=f 0 0 0 0 0 0 0 1cdb0000 0 0 0 0 0 1c0000 0 0
input_1_abs=1b
```

## Safety/result

The script was copied to `/tmp` only, executed, and removed. No event device
was opened, no input was consumed, and no persistent files or device state were
changed. The differing event numbering is now captured as a first-class target
fact; later input watching must rank by `phys`/capabilities rather than assume
an event number.

## Persistent B14 payload gate

After the B14 image was built externally, it was staged at
`ur0:/linux-toolkit/vita-toolkit-native-b14.squashfs` on the Vita and on the
PSTV removable exFAT payload store. The Vita image read back as:

```text
sha256=86a2e3a5056e2659729d39b3661afc282f0c7629e4f3f76556365bf9b15d786a
bytes=10604544
```

The Vita then mounted `/dev/mmcblk0p12` and the B14 image read-only and ran the
same inventory from `/opt/vita-toolkit`. The persistent gate passed with
`input_count=2`, `cpu_online=0-3`, `faults=0`, and zero mounts after cleanup.

The PSTV persistent gate also passed with `input_count=1`, `event0` identified
as `vita_syscon_buttons`, cart PID `28289` unchanged, `fbcon=0`, four CPUs,
clean fault state, and zero mounts after cleanup.

The B14 image remains staged on both devices for subsequent read-only input
watcher work.
