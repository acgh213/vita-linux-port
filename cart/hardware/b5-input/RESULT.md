# B5 input path — PSTV hardware validation

Date: 2026-08-30T01:26:32Z
Device: PSTV `192.168.18.43`
Kernel: `6.12.0-ga49f3db6a94f-dirty #1 SMP Sat Aug 29 11:38:24 EDT 2026`

## Candidate identity

- Candidate path on PSTV: `/tmp/pstv-demo-cart-b5-input`
- Candidate SHA-256: `f1fa752c48bdbd1572fbbbb3f09f724120741a68955b79cab1ed07139f9a6005`
- Candidate size: `580180` bytes
- Candidate executable was launched through the committed start wrapper.
- Embedded known-good cart was not replaced during the candidate run.

## Controller prerequisite

The DS4 was paired in the persistent PTY-backed interactive `bluetoothctl`
workflow after the fresh Linux boot. The initial post-boot PS-button reconnect
could not work because the RAM-resident rootfs had no bond database after the
reboot; this was a persistence boundary, not a controller or kernel failure.

The re-pair produced a persisted link key and the following live state:

- Device: `A4:53:85:25:B7:13` / `Wireless Controller`
- `Paired: yes`
- `Bonded: yes`
- `Trusted: yes`
- `Connected: yes`
- HID raw node: `/dev/hidraw0`
- Gamepad: `Wireless Controller` / `/dev/input/event1`
- Motion: `Wireless Controller Motion Sensors` / `/dev/input/event2`
- Touchpad: `Wireless Controller Touchpad` / `/dev/input/event3`
- Vita syscon controller remained present at `/dev/input/event0`

## Cart input result

The candidate log reported `input sources connected` while the DS4 was live.
The operator then used the physical DS4 to control the running cart; scene
advancement was visibly observed from the controller input path. This is the
manual hardware acceptance observation for B5. The cart was not driven by
`SIGUSR1` for that observation.

The candidate remained alive during the controlled run. Machine-recorded log:

```text
frames=300 scene=1 fps=30.08 dropped=0
frames=600 scene=3 fps=29.24 dropped=0
frames=900 scene=3 fps=28.85 dropped=0
frames=1200 scene=0 fps=30.00 dropped=0
frames=1500 scene=0 fps=30.00 dropped=0
frames=1800 scene=0 fps=29.21 dropped=0
frames=2100 scene=0 fps=29.50 dropped=0
```

Final hardware state during the candidate run:

- CPUs online: `0-3`
- candidate PID: `730`
- candidate executable: `/tmp/pstv-demo-cart-b5-input`
- framebuffer ownership was managed by the wrapper

## Restoration

The candidate was stopped through the matching wrapper. The embedded
known-good cart was then restarted through `/usr/local/bin/start-demo-cart.sh`.
Restoration verified:

- known-good SHA-256:
  `4eee4b0676a546b1d576af8a7f3f97a7ad01d6e9db95a238b3a9ac785fadae0b`
- known-good executable: `/usr/local/bin/pstv-demo-cart`
- known-good PID: `790`
- framebuffer console bind: `0`
- CPUs online: `0-3`

## Result

**PASS.** The final B5 cart input implementation was launched on the PSTV,
connected to the real bonded DualShock 4 through Linux Bluetooth HID/evdev, and
used successfully for physical scene control. The candidate remained stable
and the known-good cart was restored afterward.

This record does not claim separate physical validation of every individual
motion/touchpad axis. Those nodes were enumerated and the input engine's full
axis/action/lifecycle behavior is covered by the passing host test suite.
