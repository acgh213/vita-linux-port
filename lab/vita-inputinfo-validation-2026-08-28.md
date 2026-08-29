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
