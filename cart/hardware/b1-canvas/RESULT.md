# B1 Canvas Extraction — PSTV Validation

**UTC:** 2026-08-25T04:02:56Z
**Target:** PSTV / Dolce at `192.168.18.43`
**Commit:** `f314cb1` (`refactor: extract cart canvas primitives`)

## Scope

This validates the B1 behavior-preserving extraction of logical canvas storage,
RGBA packing/mixing, clipped primitive drawing, and deterministic PPM support.
The new static ARM binary and wrapper copies were placed only in `/tmp`. No
kernel, loader, rootfs, VitaOS storage, or installed cart artifact was changed.

## Artifact

| Artifact | SHA-256 |
|---|---|
| B1 ARM binary | `0b0a1f2993d2dbb73da1a6b508422811295dab15a3466876f9e5d4b3d94803d7` |
| Captured B1 framebuffer, raw RGBA | `ec2d72afe9294dc26dd6453d612b7402a5de15fb6affc928691fa5977968d5a1` |
| Captured B1 framebuffer, PNG | `727f9c0bdbd2901e95e127730a9f56a9e8240cb8edde78a967f177992959f173` |

The B1 process was confirmed as `/tmp/pstv-demo-cart-b1-canvas` while running.

## Live cycle

1. The installed known-good cart was stopped with the hardened wrapper.
   - fbcon restored: `1`
2. B1 started through a transient wrapper copy.
   - B1 PID: `13886`
   - CPUs online: `0-3`
   - fbcon unbound: `0`
   - framebuffer capture: exactly `3,686,400` bytes
3. B1 stopped and restored fbcon: `1`.
4. The installed known-good cart was restarted.
   - final executable: `/usr/local/bin/pstv-demo-cart`
   - final PID after cleanup: `14539`
   - final CPUs online: `0-3`
   - final fbcon state: `0`
   - saved framebuffer: exactly `3,686,400` bytes

## Visual and fault review

[`b1-frame.png`](b1-frame.png) is the direct PSTV framebuffer readback. It
shows a complete Candy Vortex scene: correct RGB/trans-pastel ordering, intact
pixel-art heart and title, no console repaint, clipping, or visible corruption.

The first broad fault grep falsely matched the substring `BUG:` inside the
harmless kernel line `debug: skip boot console de-registration.` The corrected
boundary-aware scan is recorded in
[`CORRECTED-FAULT-SCAN.txt`](CORRECTED-FAULT-SCAN.txt) and reports:

```text
fault_scan=clean
```

## Result

**PASS — B1 canvas extraction preserved live PSTV rendering and framebuffer
ownership behavior.**

- [`COMMAND-OUTPUT.txt`](COMMAND-OUTPUT.txt) contains the exact stop/start and
  target-state transcript.
- [`LOCAL-ARTIFACT-SHA256.txt`](LOCAL-ARTIFACT-SHA256.txt) records the locally
  tested B1 binary hash.
