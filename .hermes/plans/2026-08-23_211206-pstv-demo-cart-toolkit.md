# PSTV Linux Demo Cart and Field Toolkit Implementation Plan

> **For Hermes:** Use `subagent-driven-development` to execute this plan task-by-task. Use strict RED → GREEN → REFACTOR for new behavior, followed by spec-compliance review and code-quality review. Hardware-facing milestones require controller verification on the real PSTV; subagent self-reports are not sufficient.

**Goal:** Build a reproducible PSTV Linux cart image that presents polished interactive 2D and software-rendered 3D chambers, provides a compact networked Linux field toolkit, and establishes a stable platform surface for future game and application ports.

**Architecture:** Preserve the production kernel/loader baseline and the known-good v0.1 cart. Evolve the cart into a small process-oriented runtime with direct fbdev output, normalized evdev input, a static scene registry, deterministic capture, telemetry, and authenticated remote control through SSH plus a local Unix socket. Package the runtime and essential tools in a separate Buildroot cart profile; put large tools and future ports into verified read-only SquashFS packs. TinyGL is the first 3D renderer; hardware GPU work remains a separate clean-room research lane.

**Tech Stack:** Linux 6.12 ARMv7, Buildroot external tree, C11, pthreads, fbdev, evdev, Unix-domain sockets, jserv TinyGL pinned to commit `c2e48591a6bfba1a85f1b87a78dcf1abf7dff57a`, POSIX shell, SquashFS/loop mounts, OpenSSH, curl/TLS, host sanitizers, and real PSTV validation at `root@192.168.18.43`.

---

## 1. Product definition

The cart has four explicit faces:

1. **Show** — intentional 2D and 3D chambers, not a permanent diagnostic dashboard.
2. **Tools** — hardware inspection, input/storage/network diagnostics, tracing, and benchmarks.
3. **Network** — HTTPS, SSH/SFTP/SCP, transfer and diagnostic clients, and remotely invoked cart actions.
4. **Ports** — a narrow framebuffer/input/time compatibility surface for DoomGeneric, Quake software, TinyGL programs, and later experiments.

The visual attract mode remains the default experience. Toolkit UI and telemetry appear only when summoned.

## 2. Confirmed baseline and measured constraints

- Outer production commit: `c071137708a0b157deaae996f7ea01dc74b9c92e`.
- Production kernel gitlink: `996f030022f021b19e5c9182fca5cc1c60300969`.
- Production payload/loader gitlink: `435791fd6aa70d2458f48be893358f106be0234d`.
- Hardware: four Cortex-A9 cores, approximately 481 MiB usable RAM, 1280×720 32-bit simple framebuffer.
- Existing cart: 320×180 logical surface, four scanline workers, nearest-neighbor 4× scale, sustained 30.04 fps.
- Existing live soak: 24,000 frames, CPUs `0-3` online, zero panic/oops/lockup signatures.
- TinyGL live PSTV benchmark, lit gears plus output-buffer copy:
  - 320×180: 111.4 fps
  - 640×360: 43.9 fps
  - 1280×720: 11.2 fps
- Full 1280×720 RGBA frame write: 3,686,400 bytes. At 30 fps this is 110.6 MB/s before rendering traffic.
- Immediate graphics route: CPU software rendering to a lower-resolution surface, then scale/present.
- Hardware GPU: SGX543MP4+ Series5XT. Current Mesa PowerVR support is not a drop-in driver. No leaked, confidential, or license-unclear source may be used.

## 3. Non-negotiable guardrails

- Do not modify the dirty evidence-bearing checkout at `/home/cassie/projects/vita-linux-r0-clean`.
- Perform cart work in `/home/cassie/projects/vita-wt/pstv-demo-cart` on branch `pstv-demo-cart`.
- Preserve `buildroot-vita/configs/vita_defconfig` as the production/recovery profile.
- Add a separate `vita_cart_defconfig`; do not silently inflate the recovery image.
- Never commit WiFi configuration, SSH host keys, authorized keys, tokens, or other contents from `buildroot-vita/board/vita/local/`.
- Treat VitaOS/eMMC partitions as read-only and `noauto` in the toolkit.
- Do not expose arbitrary Syscon, MMIO, block-write, or unauthenticated shell operations in the launcher.
- Remote privileged control uses existing key-only SSH. The cart control service listens only on a Unix-domain socket initially.
- Freeze v0.1 before refactoring. Every behavior-preserving extraction must keep deterministic render fixtures green.
- No `dlopen` scene ABI, ECS, general scene graph, asset database, or embedded scripting VM in the first runtime.
- A hardware claim requires real target output: command logs, framebuffer capture, artifact hash, and kernel-fault scan.

## 4. Planned repository layout

```text
vita-linux-port/
├── cart/
│   ├── Makefile
│   ├── README.md
│   ├── LICENSES.md
│   ├── include/cart/
│   │   ├── canvas.h
│   │   ├── control.h
│   │   ├── fbdev.h
│   │   ├── input.h
│   │   ├── runtime.h
│   │   ├── scene.h
│   │   └── telemetry.h
│   ├── src/
│   │   ├── main.c
│   │   ├── canvas.c
│   │   ├── control.c
│   │   ├── fbdev.c
│   │   ├── input_evdev.c
│   │   ├── runtime.c
│   │   ├── scenes_builtin.c
│   │   ├── telemetry.c
│   │   └── workers.c
│   ├── scenes/
│   │   ├── scene_2d_legacy.c
│   │   └── scene_chrome_cathedral.c
│   ├── renderers/
│   │   └── tinygl_adapter.c
│   ├── tests/
│   │   ├── test_canvas.c
│   │   ├── test_control.c
│   │   ├── test_input.c
│   │   ├── test_runtime.c
│   │   ├── test_scene_registry.c
│   │   └── fixtures/
│   ├── scripts/
│   │   ├── start-demo-cart.sh
│   │   ├── stop-demo-cart.sh
│   │   └── capture-demo-cart.sh
│   └── tools/
│       ├── cartctl.c
│       └── cart-bench.c
├── buildroot-vita/
│   ├── configs/vita_defconfig
│   ├── configs/vita_cart_defconfig
│   ├── package/pstv-demo-cart/
│   ├── package/tinygl/
│   └── board/vita/cart-overlay/
├── packs/
│   ├── README.md
│   ├── scripts/build-pack.sh
│   ├── manifests/
│   └── base-toolkit/
├── tests/
│   ├── test-worktree.sh
│   ├── test-cart-buildroot.sh
│   └── test-cart-pack.sh
└── .hermes/plans/
```

The exact split may be reduced if a module does not earn its boundary. The first refactor should not create one file per scene or a generic plug-in system.

---

# Phase A — Clean baseline and reproducible foundation

## Task A1: Repair the integration-worktree helper

**Objective:** Make `make worktree NAME=… INIT_SUBMODULES=1` produce exact recorded submodules without leaving a stale rootfs directory after a failed initialization.

**Files:**
- Modify: `Makefile:381-421`
- Create: `tests/test-worktree.sh`
- Modify: `Makefile` test target or add a dedicated `test-worktree` target

**TDD steps:**
1. Create a temporary source repository fixture with one local submodule and a fake stale rootfs source.
2. Write a failing test proving the current unsupported `--reference-if-able` invocation fails and the helper nevertheless leaves a non-submodule `linux_vita/` directory.
3. Run `./tests/test-worktree.sh`; expect failure on the old helper.
4. Replace the invalid option with supported Git behavior. Use `--reference <path>` only where the referenced repository is appropriate, or omit the optimization and favor correctness.
5. Make the recipe fail immediately if submodule initialization fails.
6. Copy `rootfs.cpio.zst` only after `linux_vita/.git` exists and the exact gitlink is checked out; label the copy as a non-reproducible convenience artifact.
7. Run the test and `make test`; expect all tests green.
8. Commit: `fix: make integration worktrees fail safely`.

**Acceptance:** A throwaway integration worktree has exact submodule HEADs and no stale directory after injected failure.

## Task A2: Record the production-derived cart baseline

**Objective:** Capture machine-readable provenance before adding cart code.

**Files:**
- Create: `cart/provenance/production-baseline.txt`
- Create: `cart/scripts/verify-baseline.sh`
- Create: `cart/tests/test_verify_baseline.sh`

**Behavior:** The verifier compares outer HEAD ancestry and recorded submodule gitlinks against the production constants. It must fail on an uninitialized, dirty, or mismatched kernel/loader checkout unless an explicit development override is supplied.

**TDD steps:**
1. Write fixture tests for matching, mismatched, missing, and dirty submodule states.
2. Run tests red.
3. Implement the smallest verifier.
4. Run tests green and run it against the actual cart worktree.
5. Record host toolchain identity and baseline artifact hashes without copying secrets.
6. Commit: `chore: record cart production baseline`.

## Task A3: Import and freeze the known-good v0.1 cart

**Objective:** Bring the existing verified demo into version control without changing rendered output.

**Files:**
- Create: `cart/src/pstv-demo-cart.c` from the verified lab source
- Create: `cart/scripts/start-demo-cart.sh`
- Create: `cart/scripts/stop-demo-cart.sh`
- Create: `cart/README.md`
- Create: `cart/Makefile`
- Create: `cart/tests/test_legacy_render.sh`

**Verification:**
1. Build host sanitizer binary with `-Wall -Wextra -Werror -fsanitize=address,undefined`.
2. Dump all six scenes at fixed frames.
3. Verify PPM dimensions, deterministic SHA-256 values within the imported revision, and non-identical scene outputs.
4. Cross-build static ARMv7 binary with the known flags.
5. Verify `file` reports ARM EABI5 hard-float and no unexpected dynamic interpreter.
6. Compare the imported source hash to the lab source before making later changes.
7. Commit: `feat: import verified PSTV demo cart v0.1`.

---

# Phase B — Reusable cart runtime without engine-shaped overreach

## Task B1: Extract canvas and pixel-format primitives

**Objective:** Move logical surfaces, color packing, drawing primitives, and deterministic PPM output behind `cart_canvas` while preserving all six fixtures.

**Files:**
- Create: `cart/include/cart/canvas.h`
- Create: `cart/src/canvas.c`
- Create: `cart/tests/test_canvas.c`
- Modify: `cart/src/pstv-demo-cart.c`

**Tests first:** clipping, RGBA byte order, mix endpoints, rectangle/circle bounds, deterministic PPM header/body length, and overflow rejection.

**Commit:** `refactor: extract cart canvas primitives`.

## Task B2: Extract a static scene registry

**Objective:** Replace central numeric switches with a compile-time registry containing scene ID, display name, preferred logical size, render callback, and capability flags.

**Files:**
- Create: `cart/include/cart/scene.h`
- Create: `cart/src/scenes_builtin.c`
- Create: `cart/scenes/scene_2d_legacy.c`
- Create: `cart/tests/test_scene_registry.c`
- Modify: `cart/src/pstv-demo-cart.c`

**Tests first:** stable order, unique IDs, wraparound next/previous selection, missing-ID error, and all six legacy fixtures unchanged.

**Do not add:** dynamic loading, external manifests, scene graph, or one source file for every legacy scene.

**Commit:** `refactor: add static cart scene registry`.

## Task B3: Make time explicit and deadline recovery deterministic

**Objective:** Animate from monotonic elapsed time while retaining fixed-step deterministic capture and preventing catch-up storms after overruns.

**Files:**
- Create: `cart/include/cart/runtime.h`
- Create: `cart/src/runtime.c`
- Create: `cart/tests/test_runtime.c`
- Modify: cart main loop

**Tests first:** fixed-step progression, scene timeout, manual hold timeout, one-frame overrun, multi-frame overrun resynchronization, and no negative sleep interval.

**Commit:** `refactor: add deterministic cart runtime clock`.

## Task B4: Replace per-frame thread creation with a persistent worker pool

**Objective:** Preserve four-way scanline rendering while eliminating eight pthread lifecycle calls per frame.

**Files:**
- Create: `cart/src/workers.c`
- Add declarations to `cart/include/cart/runtime.h`
- Create: `cart/tests/test_workers.c`

**Tests first:** exact row coverage for heights not divisible by four, no overlap, single-thread fallback, initialization failure cleanup, and deterministic output versus one-thread reference.

**Target benchmark:** compare p50/p95/max render time before and after on the live PSTV.

**Commit:** `perf: reuse cart rendering workers`.

## Task B5: Add input discovery and normalized actions

**Objective:** Discover compatible evdev nodes by capability/name rather than assuming `/dev/input/event0`, and normalize buttons into `NEXT`, `PREVIOUS`, `SELECT`, `BACK`, `MENU`, and analog axes.

**Files:**
- Create: `cart/include/cart/input.h`
- Create: `cart/src/input_evdev.c`
- Create: `cart/tests/test_input.c`
- Modify: cart main loop

**Tests first:** recorded synthetic `input_event` streams, key repeats ignored, press/release semantics, analog deadzone, disconnect/reconnect, and deterministic device ranking.

**Hardware acceptance:** PSTV Syscon controller operates the carousel; USB HID remains optional.

**Commit:** `feat: normalize PSTV cart input`.

## Task B6: Generalize framebuffer probing and present

**Objective:** Validate pixel format and stride from fbdev metadata, support logical 2×/3×/4× nearest-neighbor scaling into 1280×720, and fail safely on unsupported layouts.

**Files:**
- Create: `cart/include/cart/fbdev.h`
- Create: `cart/src/fbdev.c`
- Create: `cart/tests/test_fbdev.c`
- Modify: cart main loop and dump path

**Tests first:** 320×180→1280×720, 426×240 letterbox/crop policy, 480×270 scaling policy, 640×360→1280×720, nonstandard stride, unsupported BPP/channel order, mmap failure cleanup.

**Decision:** no bilinear scaling in the first milestone; measure before adding it.

**Commit:** `feat: add validated framebuffer presenter`.

## Task B7: Harden framebuffer ownership and restoration

**Objective:** Ensure every launch failure and termination path restores fbcon state and previous framebuffer contents.

**Files:**
- Modify: `cart/scripts/start-demo-cart.sh`
- Modify: `cart/scripts/stop-demo-cart.sh`
- Create: `cart/tests/test_fb_ownership.sh`

**Tests first:** already running, missing binary, failed launch, initially bound/unbound fbcon, stale PID, truncated saved frame, TERM timeout, idempotent stop.

Use an explicit save-size derived from probed framebuffer metadata where practical; never restore a partial file as if complete.

**Commit:** `fix: make cart framebuffer restoration transactional`.

## Task B8: Add telemetry as data, not permanent decoration

**Objective:** Collect frame timing, fps, dropped deadlines, CPU online state, memory, IP addresses, and per-scene counters for logs and an optional overlay.

**Files:**
- Create: `cart/include/cart/telemetry.h`
- Create: `cart/src/telemetry.c`
- Create: `cart/tests/test_telemetry.c`

**Tests first:** percentile calculation, rolling-window reset, malformed `/proc` data, unavailable network interface, and stable machine-readable status output.

**Commit:** `feat: add cart runtime telemetry`.

## Task B9: Add a local control socket and `cartctl`

**Objective:** Replace signal-only remote control with a strict Unix-socket protocol while retaining `SIGUSR1` compatibility.

**Commands:** `status`, `next`, `previous`, `scene <id>`, `overlay on|off`, `benchmark start|stop`, `capture <approved-name>`, and `quit`.

**Files:**
- Create: `cart/include/cart/control.h`
- Create: `cart/src/control.c`
- Create: `cart/tools/cartctl.c`
- Create: `cart/tests/test_control.c`

**Tests first:** partial reads, overlong command, unknown command, invalid scene, path traversal in capture name, socket permissions, stale socket, client disconnect, and stable status response.

**Boundary:** no arbitrary command execution and no TCP listener.

**Commit:** `feat: add local cart control protocol`.

---

# Phase C — First authored software-3D chamber

## Task C1: Package pinned TinyGL reproducibly

**Objective:** Add an MIT-licensed, checksum-pinned Buildroot package and host development build for jserv TinyGL.

**Files:**
- Create: `buildroot-vita/package/tinygl/Config.in`
- Create: `buildroot-vita/package/tinygl/tinygl.mk`
- Create: `buildroot-vita/package/tinygl/tinygl.hash`
- Modify: `buildroot-vita/Config.in`
- Modify: `cart/Makefile`
- Modify: `cart/LICENSES.md`

**Verification:** fetch pinned commit archive, verify SHA-256, static ARM build with `_GNU_SOURCE`, run raw offscreen example under host sanitizers, and confirm license material is installed.

**Commit:** `build: package pinned TinyGL renderer`.

## Task C2: Integrate a TinyGL offscreen surface

**Objective:** Render TinyGL color/depth output into a cart canvas without SDL, X11, DRM, or Mesa.

**Files:**
- Create: `cart/renderers/tinygl_adapter.c`
- Create: `cart/include/cart/tinygl_adapter.h`
- Create: `cart/tests/test_tinygl_adapter.c`

**Tests first:** create/destroy at supported sizes, allocation failure, resize rejection or safe recreation, color channel mapping, depth clear, deterministic reference triangle, and clipping.

**Commit:** `feat: add TinyGL cart renderer adapter`.

## Task C3: Implement “Chrome Cathedral” scene

**Objective:** Deliver an authored interactive 3D chamber rather than shipping stock gears.

**Visual contract:** reflective-looking geometric shrine, checkerboard/folding floor, dark-violet fog, trans-pastel lighting, animated hearts/butterflies composited in 2D, and a clear authored camera path.

**Files:**
- Create: `cart/scenes/scene_chrome_cathedral.c`
- Modify: static scene registry
- Create: deterministic geometry/material fixtures under `cart/tests/fixtures/`
- Create: `cart/tests/test_chrome_cathedral.c`

**Tests first:** deterministic fixed-step image, nonempty depth coverage, triangle count, no out-of-bounds sanitizer findings, camera action limits, and valid output at 426×240, 480×270, and 640×360.

**Commit:** `feat: add Chrome Cathedral software-3D chamber`.

## Task C4: Integrate presentation, controls, and stress mode

**Objective:** Make the complete render→composite→scale→present path interactive and measurable.

**Controls:** carousel next/previous, pause camera, rotate/orbit, toggle telemetry, and increase/decrease stress level.

**Stress ladder:** object/triangle count rises predictably until frame budget misses; report p50/p95/max and dropped deadlines.

**Hardware gate:** begin at 426×240 or 480×270. Move the default to 640×360 only if a ten-minute real-target run keeps p95 under 33.3 ms with framebuffer present included.

**Commit:** `feat: integrate interactive 3D cart chamber`.

## Task C5: Complete Milestone 1 hardware acceptance

**Objective:** Prove the reusable 2D+3D cart on real hardware before image integration.

**Commands/evidence:**
- Cross-build and SHA-256 artifacts.
- SCP transient binary/scripts to `/tmp` or `/usr/local/bin` without rebuilding the image.
- Start/stop through wrappers.
- Select every scene using Syscon controller and SSH `cartctl`.
- Capture framebuffer images for every scene.
- Run at least ten minutes in 3D and ten minutes in attract mode.
- Record frame timing, all CPUs online, memory use, network continuity, and fault scan.
- Verify exact framebuffer/fbcon restoration after stop.

**Document:** `cart/hardware/MILESTONE-1-RESULT.md` with commands, hashes, and honest failures.

**Commit:** `test: record PSTV cart milestone 1`.

---

# Phase D — Reproducible cart image and essential network toolkit

## Task D1: Add separate Buildroot cart profile

**Objective:** Preserve recovery `vita_defconfig` while creating `vita_cart_defconfig` and isolated `output-cart` build targets.

**Files:**
- Create: `buildroot-vita/configs/vita_cart_defconfig`
- Modify: `Makefile` with `rootfs-cart`, `rootfs-cart-config`, and `rootfs-cart-clean`
- Create: `tests/test-cart-buildroot.sh`

**Tests first:** production rootfs target still selects `vita_defconfig`; cart target uses its own output directory; profile switching cannot reuse the wrong `.config`; no local secrets enter generated metadata.

**Commit:** `build: add isolated Vita cart rootfs profile`.

## Task D2: Package the cart runtime into Buildroot

**Objective:** Build/install the cart, scripts, `cartctl`, init integration, and licenses from source.

**Files:**
- Create: `buildroot-vita/package/pstv-demo-cart/Config.in`
- Create: `buildroot-vita/package/pstv-demo-cart/pstv-demo-cart.mk`
- Create: `buildroot-vita/board/vita/cart-overlay/etc/init.d/S90pstv-cart`
- Modify: `buildroot-vita/Config.in`

**Verification:** clean package rebuild, installed-file manifest, shell syntax, static ARM identity, reproducible package hash, and no manually copied binary.

**Commit:** `build: integrate demo cart into Buildroot`.

## Task D3: Add lean HTTPS and network essentials

**Objective:** Make the base cart useful on the network without a package manager.

**Cart-profile packages:**
- CA certificates
- TLS-enabled curl
- iproute2
- ethtool
- iw
- usbutils
- file
- less
- iperf3
- tcpdump
- socat
- rsync
- gdbserver
- Lua only if measured image/RAM cost remains acceptable

**Steps:** add packages incrementally, record compressed rootfs delta and installed tree delta, and verify HTTPS certificate validation against a known endpoint. Never use `curl -k` as the acceptance path.

**Document:** `cart/hardware/ROOTFS-SIZE-LEDGER.md`.

**Commit:** one measured package tier per commit, not one unreviewable config dump.

## Task D4: Add a capability-based toolkit launcher

**Objective:** Expose tools by capability and availability, without shell-evaluating manifests.

**Capabilities:** system, input, framebuffer, network, transfer, storage-readonly, trace, benchmark, and development.

**Files:**
- Create a small launcher under `cart/tools/` or strict POSIX shell if no untrusted data is evaluated.
- Add tests for missing tools, denied unsafe actions, read-only mount options, and stable text output.

**Commit:** `feat: add cart toolkit launcher`.

## Task D5: Build and boot-test Milestone 2 image

**Objective:** Produce a fresh rootfs from the cart profile, embed it in the exact production kernel, boot the PSTV, and repeat platform gates.

**Acceptance:**
- artifact hashes recorded;
- warm boot and true wall-cold boot;
- all four CPUs online and active;
- WiFi, DNS, HTTPS, SSH, SCP/SFTP, and Ethernet state visible;
- cart autostarts or launches deterministically without blocking SSH;
- input, USB high-speed host, reboot, RTC, and framebuffer restoration remain green;
- no panic/oops/abort/lockup signatures;
- rootfs compressed size, boot time, free memory, and ten-minute 3D timing compared with baseline.

**Document:** `cart/hardware/MILESTONE-2-RESULT.md`.

---

# Phase E — Verified USB/network tool packs

## Task E1: Enable minimal read-only pack filesystem support

**Objective:** Add SquashFS support in a dedicated kernel topic branch without mixing it into cart userspace commits.

**Kernel files:** `linux_vita/arch/arm/configs/vita_defconfig` plus Kconfig-generated result only.

**Required config:** SquashFS with the compression used by pack builder, loop block devices, and existing USB mass-storage/SCSI prerequisites where not already production-pinned.

**Tests:** kernel config assertions, build, DTB unaffected, boot, loop-mount read-only pack from tmpfs and USB, unmount, fault scan.

**Commit/PR:** separate kernel branch `topic/pstv-cart-squashfs`; outer repo pins only after hardware gate and review.

## Task E2: Define a non-executable pack manifest format

**Objective:** Verify identity, version, architecture, minimum cart API, files, SHA-256, and declared capabilities without sourcing shell.

**Files:**
- Create: `packs/README.md`
- Create: `packs/manifests/schema.md`
- Create: parser and tests under `cart/tools/`

**Tests first:** malformed manifest, duplicate field, path traversal, wrong architecture, unsupported API, hash mismatch, unknown required capability, and oversized input.

**Commit:** `feat: define verified cart pack format`.

## Task E3: Build and mount packs transactionally

**Objective:** Download or discover a complete SquashFS pack, verify it before activation, loop-mount read-only/nodev/nosuid, and register capabilities.

**Boundary:** no individual-file package manager and no writes into `/`.

**Tests:** interrupted download, stale partial file, hash mismatch, occupied loop device, mount failure cleanup, duplicate version, rollback to previous pack, and USB removal.

**Commit:** `feat: add verified cart pack loader`.

## Task E4: Produce base and extended packs

**Base pack:** tmux, Lua, network diagnostics, gdbserver helpers, benchmarks.

**Extended/lab pack:** full GDB, nmap, fio, stress-ng, mtr/bmon, Links, optional Mesa/OSMesa experiment, and explicit unsafe lab tools kept unavailable from the normal launcher.

**Acceptance:** size ledger, RAM impact, startup time, USB and network load paths, read-only enforcement, and reproducible pack hashes.

---

# Phase F — Portability demonstrations

## Task F1: DoomGeneric chamber

Implement the cart platform callbacks for framebuffer, input, timing, and exit. WAD/data remains a separate user-supplied pack unless redistribution is clearly licensed. Verify deterministic timedemo-style behavior and sustained frame timing.

## Task F2: Quake software-renderer chamber

Port the software renderer through the same platform layer. Keep game data external. Benchmark 320×180 and 426×240 first, then raise resolution only from target measurements.

## Task F3: TinyGL compatibility examples

Add a small set of legally licensed fixed-function examples/models as pack-based chambers. Use them to document the supported GL subset rather than pretending to provide complete desktop OpenGL.

## Task F4: SDL compatibility spike

Evaluate a minimal fbdev backend or older fbcon-compatible path in a throwaway branch. Current Buildroot SDL2 KMSDRM requires DRM/GBM/EGL/GL and is not assumed to work on simplefb. Promote only if it reduces port friction without bloating or destabilizing the runtime.

---

# Phase G — Separate clean-room SGX543 research lane

This phase is not on the cart release critical path.

1. Build a source/provenance ledger using public documentation and clean-room observations only.
2. Inventory GPU clocks, resets, MMIO, IRQs, memory heaps, and IOMMU behavior read-only.
3. Establish safe power/reset sequencing.
4. Reconstruct firmware loading and command submission without confidential source.
5. Produce one validated memory transfer, then one triangle.
6. Add a command-validating DRM render node.
7. Develop compatible userspace only after the kernel contract is understood.
8. Never claim current Rogue Mesa support solves Series5XT.

The cart’s renderer interface may later gain a DRM backend, but no speculative “GPU abstraction” is added before a real second backend exists.

---

# Cross-cutting verification matrix

## Every host code task

- Observe a focused test fail for the intended missing behavior.
- Implement the minimum behavior.
- Run focused test, full cart suite, `-Wall -Wextra -Werror`, ASan, and UBSan.
- Cross-build ARMv7 hard-float where applicable.
- Review generated artifacts and `git diff --check`.
- Commit one coherent task.

## Every Buildroot task

- Start from the selected defconfig in a profile-specific output directory.
- Record `.config` diff and package-selection reasons.
- Build from clean state when package dependencies change.
- Record rootfs compressed size and installed tree size.
- Verify no contents from `board/vita/local/` are staged or printed.
- Verify license/hash files for third-party source.

## Every live PSTV task

- Acquire the existing device lock where boot/deploy is involved.
- Record kernel identity and `/sys/devices/system/cpu/online`.
- Capture exact commands and artifact SHA-256.
- Read framebuffer output back from the target; do not substitute host renders.
- Verify SSH/network continuity during graphical stress.
- Scan `dmesg` for panic, Oops, BUG, data abort, alignment exception, watchdog, and lockup signatures.
- Verify framebuffer console restoration.
- For release gates, test both warm and true wall-cold boot.

# Milestone definitions

## Milestone 1 — Reusable interactive 2D+3D runtime

- v0.1 preserved and deterministic.
- Static scene registry and modular platform layer.
- Syscon input and SSH-mediated local control.
- Chrome Cathedral on the live PSTV.
- Stable 30 fps target with p50/p95/max measurements including present.
- Ten-minute 2D and 3D soaks, zero kernel faults, full framebuffer restoration.

## Milestone 2 — Reproducible cart image

- Separate `vita_cart_defconfig` and isolated Buildroot output.
- Cart and TinyGL built from pinned source, no manual binary install.
- Validated HTTPS and essential field tools.
- Warm/cold boot and platform regression matrix green.

## Milestone 3 — Tool packs

- Read-only verified SquashFS packs from USB and network.
- Base and extended tiers with size/RAM ledgers.
- No package manager, shell-sourced manifest, or write access to VitaOS partitions.

## Milestone 4 — Portability proof

- DoomGeneric and Quake software chambers use the same platform surface.
- Data licensing boundaries documented and enforced.
- Compatibility findings feed the next port rather than accreting one-off hacks.

# Risks and explicit responses

- **Memory bandwidth dominates presentation:** render lower internally and measure complete frame time.
- **TinyGL is single-threaded:** use it for fast compatibility; build a tiled renderer later only if profiling justifies it.
- **Per-frame pthread overhead:** replace with persistent workers before assuming renderer cost.
- **Tearing without page flip/vsync:** capture and inspect real frames; document limitation. Do not promise tear-free output.
- **Initramfs RAM and size growth:** preserve recovery image and move large tools to packs.
- **Repository drift:** exact worktree/gitlink verifier blocks accidental builds from evidence-bearing descendants.
- **Buildroot profile contamination:** distinct output directories and tests prevent stale `.config` reuse.
- **Unlicensed GPU material:** reject it entirely; public repository availability is not a license.
- **Runtime overengineering:** static registry and small normalized actions only; add abstractions after a second implementation earns them.
- **Hardware-only behavior:** every milestone includes a real target gate; host tests are necessary but insufficient.

# Immediate execution slice

The work begins with:

1. Repair and test the broken integration-worktree helper encountered while creating this branch.
2. Finish exact submodule initialization and verify all recorded production pins.
3. Commit this plan and baseline verifier.
4. Import/freeze v0.1 with deterministic sanitizer-backed render tests.
5. Pause for a branch checkpoint before the first behavior-preserving runtime extraction.
