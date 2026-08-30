# B6 presentation path — PSTV hardware validation

Date: 2026-08-28
Device: PSTV `192.168.18.43`
Kernel: `6.12.0-ga49f3db6a94f #2 SMP Wed Aug 26 23:30:03 EDT 2026`

## Candidate identity

- Candidate path on PSTV: `/tmp/pstv-demo-cart-b6-presentation`
- Final candidate SHA-256: `f1fa752c48bdbd1572fbbbb3f09f724120741a68955b79cab1ed07139f9a6005`
- Known-good path: `/usr/local/bin/pstv-demo-cart`
- Known-good SHA-256: `4eee4b0676a546b1d576af8a7f3f97a7ad01d6e9db95a238b3a9ac785fadae0b`
- Final CPU state: `online=0-3`
- Final framebuffer-console state: `vtcon1 bind=0`

The known-good binary was never replaced. The candidate was copied to `/tmp`
and launched only through the committed start/stop wrappers.

## Host gates

`make -C cart complete-test CROSS_CC=arm-linux-gnueabihf-gcc` passed after the
cache and presentation changes. This included:

- ASan/UBSan canvas, scene, runtime, input, worker-pool, transition, and
  presentation tests;
- deterministic six-scene captures and fixture comparison;
- WCAG photosensitivity checks for all six transitions;
- H.264 capture-checker self-test, including localized-grid coverage;
- static ARM hard-float cross-build and ABI verification;
- Makefile capture-harness, baseline, and framebuffer-ownership tests.

The new focused contracts are:

- `tests/test_transition.c`: transition endpoints are sampled once and reused;
- `tests/test_presentation.c`: nearest-neighbor expansion produces a complete
  4× 1280×720 frame from the 320×180 logical canvas.

## Hardware cycle

The guarded cycle:

1. verified the embedded known-good SHA;
2. stopped known-good and confirmed fbcon restoration;
3. started the candidate from `/tmp` and verified `/proc/$pid/exe`;
4. captured six complete candidate scene frames;
5. stopped the candidate through the wrapper;
6. restarted the known-good binary;
7. verified final executable, fbcon state, and all four CPUs.

Every candidate raw capture was exactly `3,686,400` bytes. Final candidate
capture hashes:

| Scene | SHA-256 |
|---:|---|
| 0 | `884c83263f73a1da047077ff389d5e7453e4c63d175d02d45ec6c8e5fdabd925` |
| 1 | `3b4b31714be73f84d85fe7071985d6040fb4f4c68bf6ea123660224c946db267` |
| 2 | `4550e5252a33b1b7f5ec192c8fd376611d6ca09c0b388f25e5ab0f06b0e85408` |
| 3 | `dd884155aea268d4ae9286eb8b6e2e0f5c39c385775e7f2d2f86a270fc1dbb86` |
| 4 | `6c3b3c0b37dbba47133eded0e626359caba88372a1041a9380fe587cdeb60efe` |
| 5 | `5e2988a9705f73a8588697dc02465360feb93aa071121690721d6907e8177847` |

Known-good controls were also complete:

- before: `c3a28797f485853a4b362ad34e8e22e299da46b44188938d64ce9754f7850035`
- after: `c471906bc09d67b7b60a52f637255061b9785c4b9c636355522ba2f914246064`

The final case-sensitive fault scan found no `panic`, `Oops`, `BUG:`, data-abort,
lockup, or alignment-fault signatures. The dmesg byte comparison grew during
the lifecycle, so the cycle records that it was not byte-identical rather than
claiming an unchanged log.

## Performance result

The first B6 implementation used a full cached 1280×720 back buffer. On the
PSTV it held about 15 FPS, so it was rejected despite being a reasonable host
optimization. The final implementation:

- samples outgoing/incoming transition scenes once per 15-frame fade;
- blends the cached 320×180 logical frames;
- expands directly into `/dev/fb0` with one caller-owned row buffer;
- performs no second full-frame copy.

Final candidate 60-second run:

```text
frames=300  scene=1  fps=30.09  dropped=0
frames=600  scene=2  fps=27.87  dropped=0
frames=900  scene=3  fps=29.79  dropped=0
frames=1200 scene=5  fps=30.01  dropped=0
frames=1500 scene=0  fps=30.00  dropped=0
frames=1800 scene=1  fps=30.00  dropped=0
```

The candidate stayed alive for the full run, crossed multiple automatic
transitions, and finished with zero dropped deadlines. A shorter six-scene
cycle measured `27.95 FPS` with zero dropped deadlines.

## Visual QA

`contact-sheet.png` shows six distinct, complete scenes with the expected
pastel channel order, complete text, and no console overwrite, clipping, or
render corruption. The known-good before/after controls are also valid.

Some captures contain horizontal seams from unsynchronized `dd` reads of the
live simplefb mapping. This is the known position-varying capture artifact,
not a missing scene or malformed frame. Simplefb still has no page flip or
vsync; atomic scanout remains future work requiring a real Vita display driver.

## Result

**PASS.** The endpoint-cache and direct-linear-publish implementation is host-
green, hardware-stable at approximately 30 FPS, completes all six scene
transitions, produces complete framebuffer captures, and restores the embedded
known-good cart without changing it.

## Evidence files

- `COMMAND-OUTPUT.txt` — guarded six-scene lifecycle and hashes
- `LONG-RUN-OUTPUT.txt` — 60-second candidate run and restoration
- `contact-sheet.png` — final candidate/control visual QA sheet
- `candidate-scene-0.png` through `candidate-scene-5.png` — individual captures
- `known-good-before.png`, `known-good-after.png` — positive controls
