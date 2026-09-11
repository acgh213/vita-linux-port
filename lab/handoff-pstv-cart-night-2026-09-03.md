# PSTV / Vita evening handoff — 2026-09-03

## Completed tonight

- Host gates passed in `/home/cassie/projects/vita-linux-r0-clean`:
  - cart `complete-test`
  - root `make test CART_CROSS_CC=arm-linux-gnueabihf-gcc`
  - ARM hard-float cart cross-build
  - all 17 toolkit contract tests, invoked through `sh`
- B15 real input tests were already completed successfully and were not repeated.
- PSTV read-only preflight found Linux SSH open at `192.168.18.43`; Vita 1000
  remained in VitaOS with ports 1337/1338 reachable.
- PSTV live state before the cycle:
  - kernel `6.12.0-gc477a244a49b`
  - CPUs `0-3`
  - fbcon bound (`1`)
  - no installed cart, PID, or saved framebuffer

## Cart candidate result

Full evidence is in:

`cart/hardware/night-20260903/RESULT.md`

The frozen baseline binary (`4eee4b0676a546b1d576af8a7f3f97a7ad01d6e9db95a238b3a9ac785fadae0b`)
and repaired candidate (`f1fa752c48bdbd1572fbbbb3f09f724120741a68955b79cab1ed07139f9a6005`)
were staged under a uniquely named target `/tmp` directory.

Result: **PASS — temporary candidate lifecycle**.

- Baseline and candidate executable identities were verified through `/proc`.
- Candidate ran for about 60 seconds / 1,800 frames.
- Runtime heartbeats reported `dropped=0`.
- Candidate framebuffer capture was exactly 3,686,400 bytes.
- Candidate decoded as a coherent 1280x720 `CANDY VORTEX` scene.
- Post-run dmesg scan: 248 lines, `fault_hits=0`.
- Final `fbcon=1`, CPUs `0-3`.
- Remote temporary directory cleanup verified.

This does **not** promote the candidate. The rollback image has no persistent
cart installation, and no fresh external video artifact was available for the
real-capture photosensitivity checker.

## Repository state

A preservation snapshot is at:

`lab/housekeeping/2026-09-03-evening-state/`

The outer tree remains intentionally uncommitted/dirty in these areas:

- `linux_vita` gitlink checked out at `8017108`, while outer `main` expects
  `ed91f5c`.
- generated `dist/` payload artifacts;
- toolkit start lab record;
- new cart-night evidence;
- this handoff and the B16 plan.

Do not reset or branch-clean this state without classifying the gitlink and
preserving the untracked evidence first.

## Next work

B16 plan:

`.hermes/plans/2026-09-03-vita-hwprobe.md`

Implement it host-first as a read-only composite inventory command over the
existing toolkit surfaces. No kernel, DTB, rootfs, Bluetooth, GPU, OHCI,
battery, MMIO, storage-write, or network-mutation work is part of B16.

The next hardware cart step requires a deliberately selected
SquashFS-enabled/persistent image if we want a true promotion test. Until then,
temporary `/tmp` validation remains the safe route.
