# PSTV Hardware Promotion Evidence — A4/A5 — 2026-08-30

Target: PSTV `192.168.18.43`
Kernel lineage: `8017108b4f1c` (`topic/pstv-bluetooth-controller`)
Candidate zImage: `0654dbf3…`, 21,037,424 bytes
PSTV DTB: `e15d53bd…`, 5,917 bytes
Embedded rootfs: `f74c265d…`, 16,450,592 bytes

## A4 — DS4 classic Bluetooth HID

A genuinely interactive PTY-backed `/usr/bin/bluetoothctl` session was used. The prior piped/non-interactive session had produced `store_hint 0`, `Bonded: no`, and no persisted key.

Pairing result:

```text
new_link_key ... type 0x04 pin_len 0 store_hint 1
Bonded: yes
Paired: yes
Trusted: yes
Connected: yes
```

The device `info` file contained a persisted `Key=` line (key material not recorded). The kernel attached:

- `/dev/hidraw0`
- `Wireless Controller` (`js1`, `event1`)
- `Wireless Controller Motion Sensors` (`event2`)
- `Wireless Controller Touchpad` (`mouse0`, `event3`)
- `Registered DualShock4 controller hw_version=0x00007404 fw_version=0x00008007`

Reconnect lifecycle:

1. DS4 powered off normally.
2. BlueZ retained `Bonded: yes`; HID nodes disappeared cleanly.
3. DS4 powered on normally with PS only.
4. Automatic reconnect completed in 1 second.
5. The persisted key remained present and all HID/input nodes were recreated.

**A4: PASS.** Root cause of the earlier apparent disconnect was non-interactive `bluetoothctl` pairing, which generated a keyless temporary pairing.

## A5 — reader-detached boot/reboot

The external Type-A reader was unplugged before the cycle. Only the reader was changed; the DS4 remained untouched for the boot gate.

Preflight at `2026-08-30T00:26:17Z`:

- CPUs online: `0-3`
- Wi-Fi `mlan0`: `192.168.18.43`, UP/RUNNING
- DS4: paired, bonded, trusted, connected
- no `/dev/sd*` devices

Fresh Linux boot at `2026-08-30T00:28:08Z`:

- Linux SSH returned successfully
- CPUs online: `0-3`
- Wi-Fi `mlan0`: active at `192.168.18.43`
- `/dev/sd*` count: `0`
- `hci0`: present and powered
- no USB storage/SCSI/removable-disk attachment
- expected SD8787 signature was present: WLAN reused the Bluetooth-loaded firmware and disabled AMP function 3 before firmware access

Stability sample:

```text
samples=12 ssh_ping_failures=0
```

No kernel panic, Oops, call trace, or firmware-download failure was found. Known benign lines were excluded from the decision: the intentional AMP-function `-ENODEV`, early association retry, and documented `cmd 0x23f result=0x2`.

Clean return to VitaOS:

```text
linux_ssh_down_after=2s
vitaos_1338_open_after=10s
vitaos_1337=open
```

**A5: PASS.**

## Promotion decision

A3, A4, and A5 hardware gates pass for the candidate tuple. The outer promotion branch must retain the boot heartbeat, preserve the populated private overlay for any rebuild, and pin `linux_vita` to `8017108b4f1c` rather than the stale `9d12648` sibling fork.
