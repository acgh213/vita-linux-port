# PSTV Demo Cart — v0.1

A trans-colored, girlie-pop, 90s-to-Y2K framebuffer demoscene cart running
natively on the production four-core PSTV Linux port.

This is intentionally **not** a faux BIOS, diagnostic dashboard, desktop, or
technical status screen. The machine is used as a visual instrument: animated
pixel geometry, maximalist slogans, hearts, butterflies, chrome, plasma,
checkerboards, sparkles, and trans-flag pastels.

## Live status

- Installed executable: `/usr/local/bin/pstv-demo-cart`
- Start: `/usr/local/bin/start-demo-cart.sh`
- Stop/restore console: `/usr/local/bin/stop-demo-cart.sh`
- PID: `/run/pstv-demo-cart.pid`
- Runtime log: `/run/pstv-demo-cart.log`
- Current target performance: **30.04 fps** at 1280×720 output
- CPU state after sustained run: `0-3` online
- Fault/lockup signatures after run: none

The final deployed ARM binary SHA-256 is:

```text
4eee4b0676a546b1d576af8a7f3f97a7ad01d6e9db95a238b3a9ac785fadae0b
```

## Scenes

1. **CANDY VORTEX** — animated concentric trans-pastel tunnel, center heart,
   orbiting sparkles.
2. **GIRL MODE / MAXIMUM** — warped checkerboard, heart storm, oversized type.
3. **BUBBLEGUM / OVERDRIVE** — dark-violet metaball plasma and white stars.
4. **BUTTERFLY RIOT** — crossed animated ribbons, dense butterflies, flower and
   sparkle trails.
5. **SHE HER / HYPERDRIVE** — chrome trans-orb over a perspective synth grid.
6. **TOO MUCH / IS ENOUGH** — candy stripes and full sticker-bomb overload.

Attract mode changes scenes every eight seconds.

## Controls

- Any key event from `/dev/input/event0`: advance one scene and hold it briefly.
- `SIGUSR1` to the process: advance one scene and hold it for deterministic
  capture (`kill -USR1 $(cat /run/pstv-demo-cart.pid)`).
- `Esc` or `Q` from a compatible keyboard event source: exit.
- The start/stop wrappers are the normal control path.

Direct low-speed USB keyboards still require OHCI or a high-speed translating
hub, so automatic attract mode and SSH signal control are the reliable PSTV
paths today.

## Renderer

- C11/Linux framebuffer program; no SDL, X11, Wayland, DRM, or GPU dependency.
- ARMv7 hard-float, statically linked.
- 320×180 logical canvas, nearest-neighbor 4× expansion to the PSTV's native
  1280×720 `a8b8g8r8` framebuffer.
- Correct little-endian framebuffer memory order: R, G, B, A.
- Four pthread workers divide the logical image into scanline bands each frame.
- 2048-entry sine lookup table avoids per-pixel floating-point trig.
- Procedural visuals only; no external assets required.
- Strict `-Wall -Wextra -Werror` builds for host and target.
- Every scene passed host AddressSanitizer and UndefinedBehaviorSanitizer
  rendering.

Cross-build command:

```sh
arm-linux-gnueabihf-gcc -O3 -static -pthread \
  -Wall -Wextra -Werror -march=armv7-a -mfpu=neon -mfloat-abi=hard \
  pstv-demo-cart.c -lm -o pstv-demo-cart.arm
```

## Safe framebuffer ownership

`start-demo-cart.sh`:

1. Saves exactly 3,686,400 bytes from `/dev/fb0`.
2. Records whether fbcon was bound.
3. Unbinds `vtcon1` so console repaint cannot overwrite animation.
4. Starts the renderer under `start-stop-daemon` with PID and log files.

`stop-demo-cart.sh`:

1. Sends SIGTERM and waits for the renderer.
2. Restores the saved framebuffer bytes.
3. Rebinds fbcon only if it was originally bound.

Both the restore and relaunch paths were exercised on hardware before the final
attract-mode run.

## Hardware verification

Six scenes were explicitly stepped with `SIGUSR1` and read back as six complete
3,686,400-byte raw framebuffer captures. The fresh, tracked retest evidence is:

- [`HARDWARE-RETEST-20260825.md`](hardware/v0.1/HARDWARE-RETEST-20260825.md)
  — commands, exact artifact/capture hashes, runtime state, and fault scan.
- [`hardware-retest-contact-sheet.png`](hardware/v0.1/hardware-retest-contact-sheet.png)
  — the six-scene RGB contact sheet (raw captures remain in the lab evidence
  bundle referenced by the report).
- [`WRAPPER-VALIDATION-20260825.md`](hardware/v0.1/WRAPPER-VALIDATION-20260825.md)
  — two real-PSTV hardened-wrapper stop/restore/start cycles, including exact
  wrapper hashes and the corresponding command log.

Visual QA of the live hardware captures found:

- six distinct scenes;
- complete centered text inside safe margins;
- correct pink/blue/white/purple channels;
- exact integer pixel scaling;
- no clipping, tearing, corruption, or console overwrite.

The first hardware pass usefully exposed hand-positioned long text that exceeded
the 320-pixel logical canvas. v0.1 replaced those guesses with measured centered
text placement, then repeated all six hardware captures.

## Current boundaries

- No audio driver, so the cart is silent.
- No GPU/DRM acceleration; everything is CPU-rendered.
- Doom is not bundled yet. It belongs as a later chamber with a direct-fb
  DoomGeneric platform layer—not as the cart's organizing metaphor.
- A future network controller can provide phone/browser scene selection without
  waiting on direct low-speed USB HID support.
