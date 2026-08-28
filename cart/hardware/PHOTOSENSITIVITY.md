# Photosensitivity — findings, fixes, and hardware constraints

**Incident:** The B5 demo cart was run on real PSTV hardware and immediately
stopped: every automatic and manual scene transition produced rapid
high-contrast flashing and partial/incoherent frames. Correctly identified as
a seizure hazard.

**Status:** Three defects diagnosed and repaired. One class of artifact is
mitigated but *not* eliminated, for a hardware reason documented below.

All measurements were taken on the host against the real render code. Nothing
in this investigation was run on hardware.

---

## Defect 1 — Every scene cut was a WCAG general-flash event

There was no transition code in the cart at all. Scene N's last frame and
scene N+1's first frame were adjacent in scanout.

Relative luminance (WCAG 2.x, sRGB) either side of each cut, **before**:

| Cut | L before | L after | Δ |
|---|---|---|---|
| 0→1 | 0.6663 | 0.5003 | 0.1660 |
| 1→2 | 0.5021 | 0.2756 | 0.2265 |
| 2→3 | 0.2643 | 0.6116 | **0.3473** |
| 3→4 | 0.6129 | 0.3022 | 0.3106 |
| 4→5 | 0.3003 | 0.5820 | 0.2817 |

Threshold is Δ ≥ 0.10 where the darker frame is < 0.80. All five violated.

**After** the 15-frame eased crossfade — worst adjacent-frame delta per
transition:

| Transition | Worst Δ | Margin under threshold |
|---|---|---|
| 0→1 | 0.0195 | 5.1× |
| 1→2 | 0.0292 | 3.4× |
| 2→3 | **0.0419** | 2.4× |
| 3→4 | 0.0362 | 2.8× |
| 4→5 | 0.0348 | 2.9× |
| 5→0 | 0.0128 | 7.8× |

Enforced permanently by `make test-photosensitivity`.

### Why 15 frames and not 6

Measured on the worst cut (2→3):

| Frames | Duration | Max adjacent Δ | Result |
|---|---|---|---|
| 3 | 0.10s | 0.1599 | FAIL |
| 5 | 0.17s | 0.1185 | FAIL |
| 6 | 0.20s | 0.0948 | pass, **no margin** |
| 9 | 0.30s | 0.0672 | pass |
| **15** | **0.50s** | **0.0411** | **pass, 2.4× margin** |

6 frames clears the threshold by 5%. That is not enough headroom to survive a
scene being edited, so 15 was chosen. `CART_TRANSITION_FRAMES` is a **safety
constant, not a tuning knob** — do not shorten it without re-running the gate.

The curve is smoothstep (3t²−2t³), not linear, so the first derivative is zero
at both endpoints and neither the entry nor the exit of the fade produces a
step against the steady frames either side.

---

## Defect 2 — Manual scene advance was unrate-limited

`cart_runtime_request_next/previous` committed `scene_index` immediately with
no minimum interval. This is what turned Defect 1 from five jarring cuts into
a strobe.

| Scenario | Before | After |
|---|---|---|
| Mash NEXT 10×/sec | 10 cuts/sec | **2 cuts/sec** |
| One NEXT per rendered frame | **30 cuts/sec** | **2 cuts/sec** |
| Sustained 30/sec for 10s | 300 cuts | **19 cuts (1.9/sec)** |
| Undisturbed attract loop, 60s | 7 rotations | 7 rotations (unchanged) |

WCAG general-flash limit is 3 flashes/sec. Worst case is now 1.9/sec.

`CART_RUNTIME_MIN_CHANGE_NS` (500 ms) is a **safety constant, not a tuning
knob.** It is passed to `cart_runtime_init()` as `min_change_ns` rather than
being hardcoded, so tests can pass 0 to exercise pure stepping semantics. The
production call site in `pstv-demo-cart.c` must always pass the constant.

### Sub-case: automatic rotation immediately after a manual change

A manual advance landing just before an automatic rotation boundary produced
two cuts in rapid succession. `cart_runtime_tick` now defers the automatic
rotation while inside the cooldown rather than cutting again. Pinned by
`test_auto_rotation_respects_recent_manual_change`.

---

## Defect 3 — Partial/incoherent frames (MITIGATED, NOT FIXED)

`upscale()` wrote 3,686,400 bytes directly into the live `/dev/fb0` mmap while
the display was scanning it out. No back buffer, no vsync, no page flip.
Tearing was structural, and a torn frame *during a cut* is literally
half-old-scene / half-new-scene.

### Page-flip and vsync are unavailable on this hardware

PSTV runs `CONFIG_FB_SIMPLE=y` (`arch/arm/configs/*vita*:88`) with
`compatible = "simple-framebuffer"` (`arch/arm/boot/dts/vita2000.dts:19`).

In `drivers/video/fbdev/simplefb.c`:

- **line 587:** `info->var.yres_virtual = params.height` — virtual height
  equals visible height, so there is no off-screen region to flip to.
- **line 117:** `simplefb_ops` provides only `fb_destroy` and `fb_setcolreg`
  plus `FB_DEFAULT_IOMEM_OPS`. There is **no `.fb_pan_display` and no vsync
  ioctl.**

`FBIOPAN_DISPLAY` and `FBIO_WAITFORVSYNC` will therefore fail. Do not add code
that calls them expecting success.

### What was done instead, and why it is target-specific

The framebuffer mapping is **write-combining, not cached**:

- `simplefb.c:598` → `ioremap_wc()`
- `fb_io_mmap()` → `pgprot_framebuffer()` →
  `include/asm-generic/video.h:23` → `pgprot_writecombine()`

Write-combining memory has no read-allocate and merges stores through a small
buffer. The 172,800 scattered strided memcpys `upscale()` issues are close to
a worst case for it; one linear sweep is the ideal pattern. Frames are now
composed in a cached back buffer and blitted once.

**This is a pessimisation on a cached host framebuffer** — measured at
+0.23 ms/frame, with the host fb-touch window becoming 2.1× *longer*. It is a
win only on the real target's write-combining memory. Do not "optimise" it
away based on host timings.

**Tearing is reduced, not eliminated.** Fully fixing it requires a real Vita
display driver with double buffering and vsync. That is future work and was
explicitly not attempted here.

---

## Cleared — investigated and found not to be causes

- **Worker pool.** `cart_worker_pool_dispatch` blocks on `stop_barrier`
  before returning, so `low[]` is fully coherent per frame. Bands were not
  racing.
- **Input edge detection.** `cart_input_key_emits_action` accepts only
  `value == 1`; evdev autorepeat (`value == 2`) was already filtered, and
  `tests/test_input_actions.c:203-207` pins this.
- **Scene content.** Maximum intra-scene frame-to-frame Δ across all six
  scenes is 0.0014, with zero flash pairs in 3 seconds each. The content was
  never the hazard — the seams were.

---

## Performance headroom

A crossfade renders **two** scenes per frame plus a blend. If frames drop
*during* a fade, the fade itself becomes a flash hazard, because a dropped
frame mid-fade is a large luminance step. Measured on host:

| | Cost | Budget used |
|---|---|---|
| Worst steady frame (scene 2) | 1.377 ms | 4.1% |
| Worst fade frame (2→3) | 2.048 ms | 6.1% |
| Fade / steady ratio | 1.49× | |
| Headroom before overrun | **16.3×** | |

The blend is cheap; the second scene render dominates. Even a 10× slower ARM
core leaves the worst fade frame inside the 33.3 ms budget.

**This remains the most important thing to watch on hardware.** Host timings
cannot predict PSTV memory behaviour.

---

## Hardware re-validation protocol

The cart must **not** be viewed directly on first re-run. Required order:

1. Capture the PSTV output to video with nobody watching live.
2. Run the recording through the same luminance analysis used in
   `tests/test_photosensitivity.c`. On-device results must match host
   predictions.
3. Only after the capture measures clean does anyone watch it.

This inverts the sequence that caused the original incident, where the failure
mode was discovered by a person looking at it.

---

## Files

- `src/transition.c`, `include/cart/transition.h` — eased crossfade
- `src/runtime.c`, `include/cart/runtime.h` — scene-change rate limiter
- `src/pstv-demo-cart.c` — crossfade wiring, back buffer
- `tests/test_photosensitivity.c` — permanent WCAG gate (`make test-photosensitivity`)
- `tests/test_runtime.c` — rate-limit and rotation-deferral tests
- `docs/plans/2026-08-27-pstv-transition-flash-repair.md` — full plan
