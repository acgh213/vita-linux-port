# PSTV Demo Cart — Hardware Retest

**UTC:** 2026-08-25T02:57:49Z
**Scope:** Fresh actual-hardware validation after the A3 fixture-publication safety hardening (`583c806`). This test did not deploy a kernel, loader, payload, or rootfs; it exercised the already-live production cart over SSH and wrote only temporary framebuffer captures under `/tmp`, removed after retrieval.

## Preconditions

| Check | Result |
|---|---|
| Outer production provenance verifier | PASS |
| Kernel gitlink / live kernel | `996f030022f021b19e5c9182fca5cc1c60300969` / `6.12.0-g996f030022f0` |
| Loader gitlink | `435791fd6aa70d2458f48be893358f106be0234d` |
| Host-built cart SHA-256 | `4eee4b0676a546b1d576af8a7f3f97a7ad01d6e9db95a238b3a9ac785fadae0b` |
| Live PSTV cart SHA-256 | `4eee4b0676a546b1d576af8a7f3f97a7ad01d6e9db95a238b3a9ac785fadae0b` (exact match) |
| CPU online set | `0-3` |
| Framebuffer | `1280,720` |
| fbcon binding while cart ran | `0` (unbound, as required) |

## Host gates

All passed from `/home/cassie/projects/vita-wt/pstv-demo-cart`:

- `make -C cart test`: ASan/UBSan host render; six deterministic, non-identical fixed-frame fixtures; imported-source hash check; ARM cross verification.
- `sh cart/scripts/verify-baseline.sh --repo "$PWD"`: production outer/kernel/loader provenance and clean submodules.
- `sh cart/tests/test_make_harness.sh`: A3 hostile fixture-publication/cleanup regression harness.
- Static ARM hard-float artifact SHA-256 matched the known-good production binary exactly.

## Live framebuffer capture gate

The running cart was advanced with `SIGUSR1` before each capture. Every readback used exactly `dd if=/dev/fb0 bs=4096 count=900`, yielding **3,686,400 bytes** (1280 × 720 × 4) each.

| Capture | SHA-256 |
|---|---|
| `scene-0.raw` | `72e4fad25753fa5adc80cdcf07878208a0b53d5fee0d2a9f4bd5aef3a44a2ef8` |
| `scene-1.raw` | `b48e3b011baaa8558a721d13c7f1f994d7c33d4416e2dfb55b0ac0752e3ab38a` |
| `scene-2.raw` | `a3ca144b107f9ee19162af6f16ef87c8e3f4a64f3b7a68cc1dfcac93d0d0af05` |
| `scene-3.raw` | `13c362b2560fcba9ecaac1a8ed3da0b39b21902aeaddfd0c3ceb0d57e40821d9` |
| `scene-4.raw` | `1187cf1b7586740884953cb30c6446f56469c48d48a158a2426ce9c5fef22a03` |
| `scene-5.raw` | `e27fd7f0477942bedf9984f4e5b0b49696eaf5dfbfc2343b3fdcea7689b49ccc` |

All six hashes are distinct. The generated `hardware-retest-contact-sheet.png` is a 960×360 RGB contact sheet with SHA-256 `b4df7d916a61aff3846b1471ba356868af32fb4253a2c97949479aa0faf2b800`.

## Visual QA

The sheet shows six distinct, complete scenes: CANDY VORTEX; GIRL MODE / MAXIMUM; BUBBLEGUM / OVERDRIVE; BUTTERFLY RIOT; SHE HER / HYPERDRIVE; and TOO MUCH / IS ENOUGH.

- Trans-pastel pink/blue/white/purple channel order is correct.
- Text is centered and fully within the logical canvas.
- No clipping, tearing, framebuffer corruption, or fbcon/tty overwrite is visible.
- Renderer remained alive after the capture sequence; `online=0-3` and `vtcon1/bind=0` remained true.
- Kernel fault-signature scan (Oops, BUG, unhandled kernel fault, panic, soft/hard lockup) returned no matches.

## Result

**PASS — the current production cart and its A3 housekeeping hardening are ready.**
