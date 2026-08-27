# B3 runtime clock — PSTV hardware validation (2026-08-25)

Candidate: deterministic cart runtime clock (`cart_runtime`), branch
`pstv-demo-cart-b3-runtime`, commit `32e9247` on top of `origin/main` `789b5a4`.

## Identity

| Item | Value |
|---|---|
| Candidate path on PSTV | `/tmp/pstv-demo-cart-b3-runtime` |
| Candidate SHA-256 | `4f27774edf069e1b48fe1612bf5b6391e42debb94b72d3e10e171a09a10e8121` |
| Known-good path | `/usr/local/bin/pstv-demo-cart` |
| Known-good SHA-256 | `4eee4b0676a546b1d576af8a7f3f97a7ad01d6e9db95a238b3a9ac785fadae0b` |
| Captured scene (main capture) | CANDY VORTEX (scene 1, attract mode) |
| Main raw frame bytes / SHA-256 | 3,686,400 / `8cc69123b85b8ea7806cdcfdbf44309077867166a8af5cf6ac3c6a37db61ea86` |
| Kernel | `6.12.0-g996f030022f0` (SMP production, 4 CPUs) |
| Boot context | True cold boot: VitaOS reboot → 30 s settle → `launch PLGINLDR0` → Linux |

## Lifecycle (guarded cycle, self-restoring script)

- Preflight: known-good pid alive, `online=0-3`, fbcon `0`, known-good SHA
  matches provenance, saved frame 3,686,400 bytes.
- Stop known-good → fbcon restored to `1`; start candidate → pid `4625`,
  `readlink -f /proc/$pid/exe` == `/tmp/pstv-demo-cart-b3-runtime` (right
  binary proven live), candidate SHA matches, fbcon `0`, CPUs `0-3`.
- Frame captured: exactly 3,686,400 bytes.
- Restore: candidate stopped → fbcon `1` → known-good restarted (pid `5184`,
  exe back at `/usr/local/bin/pstv-demo-cart`, fbcon `0`, CPUs `0-3`, saved
  frame intact).
- Fault scan (case-sensitive `panic|Oops|BUG:|Unhandled fault|Data abort|
  alignment exception|soft lockup|hard LOCKUP`): **clean**.

## Visual inspection

The main capture shows a complete, correctly-rendered CANDY VORTEX scene:
full-frame, correct trans-pastel channels, centered heart, no clipping, no
console/fbcon overwrite, no corruption. A horizontal tear is present through
the center of the main capture.

### Tear characterization (not a B3 regression)

A 5-capture sequence (~1 s apart) of the candidate showed tears at *different*
vertical positions (lower-third, top, bottom) with clean frames interspersed
(captures 3 and 5 pixel-perfect). The tear is a **capture artifact**: the
unsynchronized `dd` read of `/dev/fb0` spans ~33 ms render periods and
sometimes straddles a worker-band write boundary (the 4 pthread workers write
scanline bands). The display itself is unaffected by the capture.

**Positive control:** the identical 5-capture sequence against the
known-good production cart (`4eee4b06…`) tore in **4 of 5** captures, same
sporadic, position-varying signature. The known-good binary exhibits the tear
at a higher rate than the candidate; therefore the artifact is inherent to
capturing a live framebuffer, not a runtime-clock regression.

### Runtime clock exercised on hardware

During the candidate 5-capture sequence the scene auto-advanced from CANDY
VORTEX to GIRL MODE MAXIMUM on the attract-mode cadence — the deterministic
runtime clock's scene selection and auto-advance path ran on real hardware
with no faults.

## Deployment finding (this cycle)

`/usr/local/bin/pstv-demo-cart` and the start/stop wrappers are **not
embedded in the zImage rootfs** (initramfs). They are copied into RAM at
deploy time and **vanish on every reboot**. A cold boot therefore requires
re-deploying the known-good binary + wrappers before the cart can run:

```sh
# after any reboot (Linux up, SSH reachable):
mkdir -p /usr/local/bin
scp lab/demo-cart/pstv-demo-cart.arm      root@192.168.18.43:/usr/local/bin/pstv-demo-cart
scp cart/scripts/{start,stop}-demo-cart.sh root@192.168.18.43:/usr/local/bin/
ssh root@192.168.18.43 'chmod +x /usr/local/bin/pstv-demo-cart /usr/local/bin/start-demo-cart.sh /usr/local/bin/stop-demo-cart.sh; /usr/local/bin/start-demo-cart.sh'
```

The cold boot also cleared a stray double cart instance that had been running
untracked by the pidfile on the previous session (two init-children, one from
the earlier A3-era start); the fresh boot starts exactly one known-good cart.

## Statements

- **The known-good cart was not replaced.** `/usr/local/bin/pstv-demo-cart`
  SHA-256 remains `4eee4b06…` (verified in preflight and final state).
- Candidate evidence committed alongside this RESULT.md: main capture PNG,
  5-capture + control contact sheets, both cycle command outputs.

## Files

- `COMMAND-OUTPUT-20260825.txt` — main guarded cycle output
- `MULTICAP-OUTPUT-20260825.txt` — candidate 5-capture + known-good control outputs
- `b3-frame.png` — main capture
- `b3-multicap-sheet.png` — candidate captures 1–5
- `ctrl-multicap-sheet.png` — known-good captures 1–5 (positive control)
