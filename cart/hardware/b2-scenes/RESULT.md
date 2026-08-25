# B2 Static Scene Registry — PSTV Validation

**UTC:** 2026-08-25T04:34:06Z
**Target:** PSTV / Dolce at `192.168.18.43`
**Code commit:** `6e908a6` (`refactor: add static cart scene registry`)

## Scope

This validates the B2 runtime refactor: a fixed, compile-time scene registry selects the existing six legacy scenes through phase-aware render callbacks. It does not change framebuffer ownership wrappers, kernel, loader, rootfs, input discovery, or the production-installed cart binary.

The candidate was cross-built on the host and copied only to `/tmp/pstv-demo-cart-b2-scenes`. The pre-existing normal cart was stopped using the committed wrapper, B2 was started through the same wrapper with `CART_BIN` overridden, one raw framebuffer was captured, and B2 was stopped. The normal known-good cart was then restarted.

## Candidate identity

| Item | Value |
| --- | --- |
| Candidate path | `/tmp/pstv-demo-cart-b2-scenes` |
| Candidate SHA-256 | `6402538362ed8c37058904fc2c73afc32c2f025ca24bd429910e6421704d090b` |
| Candidate format | Static ARM ELF32, EABI5, hard-float |
| Captured scene | Candy Vortex at attract-mode runtime frame |
| Raw framebuffer size | `3,686,400` bytes (`1280 × 720 × 4`) |
| Raw framebuffer SHA-256 | `689ea49a6c0c1481120819370eddd387923d082e97647c91589ce9b7b28a0f08` |

## Observed lifecycle

1. Before the cycle, the known-good `/usr/local/bin/pstv-demo-cart` was alive, all CPUs were online (`0-3`), fbcon was unbound (`0`), and the saved framebuffer was complete (`3,686,400` bytes).
2. Stopping the normal cart restored fbcon (`1`).
3. B2 started as PID `5551` from the `/tmp` candidate path. It kept fbcon unbound and CPUs online (`0-3`).
4. The B2 raw framebuffer was exactly `3,686,400` bytes.
5. Stopping B2 restored fbcon (`1`). Restarting the normal cart restored its normal state: PID `6229`, executable `/usr/local/bin/pstv-demo-cart`, fbcon unbound (`0`), CPUs `0-3`, and a complete saved frame.
6. The targeted kernel fault scan was clean.

## Visual inspection

`b2-frame.png` is the converted capture. It shows a complete, coherent Candy Vortex scene: crisp heart and title treatment, correct trans-pastel pink/blue/white channel ordering, and no visible fbcon overwrite, clipping, tearing, or corruption.

## Evidence files

- `COMMAND-OUTPUT-20260825.txt` — exact remote lifecycle output and fault scan.
- `b2-frame.png` — converted 1280×720 B2 framebuffer capture.

The normal production cart was not replaced. It remained the known-good binary with SHA-256 `4eee4b0676a546b1d576af8a7f3f97a7ad01d6e9db95a238b3a9ac785fadae0b` after this run.
