# PSTV Demo Cart — Transition Flash Hazard Repair Plan

> **For Hermes:** Use subagent-driven-development skill to implement this plan task-by-task.

**Goal:** Eliminate the photosensitive-seizure hazard in the PSTV B5 demo cart's scene transitions, with every fix proven green on the host before any hardware run.

**Architecture:** Three independent defects stack to produce the observed flashing. Fix them in safety order — (1) rate-limit scene changes in `cart_runtime` so the hazard cannot be driven at high frequency, (2) add a luminance-bounded eased crossfade so no adjacent frame pair exceeds the WCAG threshold, (3) eliminate torn/incoherent frames by staging the blit through a back buffer. All three are verifiable on the host; a new `test-photosensitivity` gate makes the safety property permanently enforced rather than manually checked.

**Tech Stack:** C11, POSIX threads, `/dev/fb0` (simplefb), existing `make test` / `complete-test` harness.

**Worktree:** `/home/cassie/projects/vita-wt/pstv-demo-cart-b5-input`
**Branch:** `pstv-demo-cart-b5-input` (B5 baseline `2ca953f`)
**Cart root:** `cart/`

---

## Evidence Base

All figures below were measured on the host against the real B5 render code. Reproduction sources are in this plan; nothing was run on hardware.

### Defect 1 — Every scene cut is a WCAG general-flash event

Measured relative luminance (WCAG 2.x sRGB) on the frame pair either side of each cut:

| Cut | L before | L after | Δ | Verdict |
|---|---|---|---|---|
| 0→1 | 0.6663 | 0.5003 | 0.1660 | flash pair |
| 1→2 | 0.5021 | 0.2756 | 0.2265 | flash pair |
| 2→3 | 0.2643 | 0.6116 | **0.3473** | flash pair (worst) |
| 3→4 | 0.6129 | 0.3022 | 0.3106 | flash pair |
| 4→5 | 0.3003 | 0.5820 | 0.2817 | flash pair |

Threshold: Δ ≥ 0.10 with darker frame < 0.80. **All five cuts violate.** `grep -rni "fade\|crossfade\|transition\|blend" src/ include/ scenes/` returns nothing — there is no transition code in the cart at all.

### Defect 2 — Manual scene advance is unrate-limited

Simulated against real `cart_runtime.c`:

```
mash NEXT 10x/sec   -> 10 cuts in 1.0s     (WCAG limit: 3/sec)
one NEXT per frame  -> 30 cuts in 1.0s     -> 30 Hz full-screen luminance inversion
```

`cart_runtime_request_next` commits `scene_index` immediately with no minimum interval. This is what converts Defect 1 from "five jarring cuts" into a strobe.

### Defect 3 — `upscale()` writes directly into live scanout

`grep -rn "WAITFORVSYNC\|PAN_DISPLAY\|yres_virtual\|yoffset\|FBIOPAN" src/ include/` → **NONE FOUND**.

`upscale(fb)` memcpy's 3,686,400 bytes straight into the `/dev/fb0` mmap while the display scans it out. No back buffer, no vsync, no page flip. Tearing is structural. A torn frame *during a cut* is literally half-old-scene / half-new-scene — the "partial/incoherent frames" observed on hardware.

### Cleared — not causes

- **Worker pool.** `cart_worker_pool_dispatch` blocks on `stop_barrier` before returning; `low[]` is fully coherent per frame. Bands are not racing.
- **Input edge detection.** `cart_input_key_emits_action` accepts only `value == 1`; evdev autorepeat (`value == 2`) is already filtered, and `tests/test_input_actions.c:203-207` pins this.
- **Scene content.** Max intra-scene frame-to-frame Δ across all six scenes is 0.0014, zero flash pairs in 3s each. The content is safe; the *seams* are the hazard.

### Crossfade duration — measured, not guessed

Eased (smoothstep) crossfade on the worst cut (2→3):

| Frames | Duration | Max adjacent Δ | Violations |
|---|---|---|---|
| 3 | 0.10s | 0.1599 | 2 FAIL |
| 5 | 0.17s | 0.1185 | 1 FAIL |
| **6** | **0.20s** | **0.0948** | **0 PASS** (marginal) |
| 9 | 0.30s | 0.0672 | 0 PASS |
| **15** | **0.50s** | **0.0411** | **0 PASS** |

At 15 frames all six cuts pass with max Δ 0.0411 — a 2.4x margin under threshold. **15 frames (0.50s) is the chosen duration.** 6 frames technically passes but sits 5% under the line with no margin for scene changes; reject it.

### Hardware constraint discovered — Defect 3 cannot be fixed by page flip

PSTV runs `CONFIG_FB_SIMPLE=y` (`arch/arm/configs/*vita*:88`), device tree declares `compatible = "simple-framebuffer"` (`arch/arm/boot/dts/vita2000.dts:19`).

In `drivers/video/fbdev/simplefb.c`:
- `info->var.yres_virtual = params.height` (line 587) — virtual height equals visible height, so there is no off-screen region to flip to.
- `simplefb_ops` (line 117) provides only `fb_destroy` and `fb_setcolreg` plus `FB_DEFAULT_IOMEM_OPS`. **No `.fb_pan_display`, no vsync ioctl.**

`FBIOPAN_DISPLAY` and `FBIO_WAITFORVSYNC` will fail on this driver. Task 7 therefore uses a **CPU-side back buffer + single fast blit**, which shrinks the tear window without needing driver support. Full tear elimination would require writing a real Vita display driver — explicitly out of scope here and noted as future work.

---

## Task Sequence

### Task 1: Add failing test for scene-change rate limiting

**Objective:** Pin the requirement that scene changes cannot occur faster than 3/sec, before any implementation.

**Files:**
- Modify: `cart/tests/test_runtime.c`

**Step 1: Add the test function**

Insert before `main()` in `cart/tests/test_runtime.c`:

```c
static void test_manual_change_rate_limited(void)
{
    struct cart_runtime runtime = fresh_runtime();
    size_t previous;
    int cuts = 0;

    /* STEP is the frame period; ten requests inside one scene period must
     * not produce ten scene changes. WCAG general-flash limit is 3/sec. */
    cart_runtime_tick(&runtime, START);
    previous = runtime.scene_index;
    for (int index = 0; index < 10; index++) {
        uint64_t now = START + (uint64_t)index * STEP;

        cart_runtime_tick(&runtime, now);
        cart_runtime_request_next(&runtime, now, SCENE_PERIOD);
        if (runtime.scene_index != previous)
            cuts++;
        previous = runtime.scene_index;
    }
    expect(cuts <= 1, "ten rapid requests over one frame period yield at most one cut");
}

static void test_rate_limit_releases_after_interval(void)
{
    struct cart_runtime runtime = fresh_runtime();
    size_t first;

    cart_runtime_tick(&runtime, START);
    cart_runtime_request_next(&runtime, START, SCENE_PERIOD);
    first = runtime.scene_index;
    expect(first == 1, "first request is honoured immediately");

    /* Still inside the cooldown: suppressed. */
    cart_runtime_request_next(&runtime, START + STEP, SCENE_PERIOD);
    expect(runtime.scene_index == first, "request inside cooldown is suppressed");

    /* Past the cooldown: honoured again. */
    cart_runtime_request_next(&runtime, START + CART_RUNTIME_MIN_CHANGE_NS,
                              SCENE_PERIOD);
    expect(runtime.scene_index == 2, "request after cooldown is honoured");
}
```

Register both in `main()` alongside the existing calls:

```c
    test_manual_change_rate_limited();
    test_rate_limit_releases_after_interval();
```

**Step 2: Run to verify failure**

```sh
cd /home/cassie/projects/vita-wt/pstv-demo-cart-b5-input/cart
make test-runtime
```

Expected: compile error — `CART_RUNTIME_MIN_CHANGE_NS` undeclared. That is the correct RED state.

**Step 3: Commit the failing test**

```sh
git add cart/tests/test_runtime.c
git commit -m "test: pin scene-change rate limit requirement (RED)"
```

---

### Task 2: Implement the scene-change rate limiter

**Objective:** Make Task 1 green — no more than one scene change per 500 ms.

**Files:**
- Modify: `cart/include/cart/runtime.h`
- Modify: `cart/src/runtime.c`

**Step 1: Extend the runtime struct and add the constant**

In `cart/include/cart/runtime.h`, add after the includes:

```c
/* Photosensitivity guard: minimum interval between committed scene changes.
 * WCAG 2.x general-flash limit is 3 flashes/sec; 500 ms gives 2/sec with
 * margin. See docs/plans/2026-08-27-pstv-transition-flash-repair.md. */
#define CART_RUNTIME_MIN_CHANGE_NS UINT64_C(500000000)
```

Add to `struct cart_runtime`, after `uint64_t manual_hold_until_ns;`:

```c
    uint64_t last_change_ns;
    int had_change;
```

**Step 2: Gate both request functions**

In `cart/src/runtime.c`, add this helper above `cart_runtime_request_next`:

```c
static int change_allowed(const struct cart_runtime *runtime, uint64_t now_ns)
{
    if (!runtime->had_change)
        return 1;
    if (now_ns < runtime->last_change_ns)
        return 0;
    return now_ns - runtime->last_change_ns >= CART_RUNTIME_MIN_CHANGE_NS;
}
```

In **both** `cart_runtime_request_next` and `cart_runtime_request_previous`, insert immediately after the existing NULL/scene_count guard:

```c
    if (!change_allowed(runtime, now_ns))
        return;
```

and immediately after `runtime->scene_index = runtime->manual_scene_index;`:

```c
    runtime->last_change_ns = now_ns;
    runtime->had_change = 1;
```

`memset` in `cart_runtime_init` already zeroes both new fields — no init change needed.

**Step 3: Run to verify pass**

```sh
make test-runtime
```

Expected: all tests pass, including the two new ones.

**Step 4: Verify no regression across the suite**

```sh
make test
```

Expected: every existing gate still green.

**Step 5: Commit**

```sh
git add cart/include/cart/runtime.h cart/src/runtime.c
git commit -m "fix: rate-limit scene changes to 2/sec (photosensitivity guard)"
```

---

### Task 3: Also gate the automatic rotation behind the rate limiter

**Objective:** Close the path where a manual change immediately followed by an automatic rotation boundary produces two cuts in quick succession.

**Files:**
- Modify: `cart/tests/test_runtime.c`
- Modify: `cart/src/runtime.c`

**Step 1: Write the failing test**

```c
static void test_auto_rotation_respects_recent_manual_change(void)
{
    struct cart_runtime runtime = fresh_runtime();
    size_t manual;

    /* Manually advance one frame period before the automatic boundary. */
    cart_runtime_tick(&runtime, SCENE_PERIOD - STEP + START);
    cart_runtime_request_next(&runtime, SCENE_PERIOD - STEP + START, 0);
    manual = runtime.scene_index;

    /* Crossing the automatic boundary must not immediately cut again. */
    cart_runtime_tick(&runtime, SCENE_PERIOD + START);
    expect(runtime.scene_index == manual,
           "auto rotation defers while inside the change cooldown");
}
```

Register it in `main()`.

**Step 2: Run to verify failure**

```sh
make test-runtime
```

Expected: FAIL — `auto rotation defers while inside the change cooldown`.

**Step 3: Implement**

In `cart_runtime_tick`, replace the automatic-index assignment branch:

```c
    if (runtime->manual_hold_active) {
        runtime->scene_index = runtime->manual_scene_index;
    } else {
        size_t automatic = (size_t)((elapsed_ns / runtime->scene_period_ns) %
                                    runtime->scene_count);

        if (automatic != runtime->scene_index &&
            !change_allowed(runtime, now_ns)) {
            /* Hold the current scene until the cooldown expires. */
        } else {
            if (automatic != runtime->scene_index) {
                runtime->last_change_ns = now_ns;
                runtime->had_change = 1;
            }
            runtime->scene_index = automatic;
        }
    }
```

Move `change_allowed` above `cart_runtime_tick` so it is in scope.

**Step 4: Run to verify pass**

```sh
make test-runtime && make test
```

Expected: all green.

**Step 5: Commit**

```sh
git add cart/tests/test_runtime.c cart/src/runtime.c
git commit -m "fix: defer automatic rotation inside the scene-change cooldown"
```

---

### Task 4: Add the photosensitivity measurement harness

**Objective:** Make the WCAG luminance property machine-checkable, so it can never silently regress.

**Files:**
- Create: `cart/tests/test_photosensitivity.c`
- Modify: `cart/Makefile`

**Step 1: Create the harness**

Create `cart/tests/test_photosensitivity.c`:

```c
/* WCAG 2.x general-flash gate.
 *
 * A flash pair is any adjacent frame pair whose relative luminance differs
 * by >= 0.10 where the darker frame is below 0.80. This gate renders the
 * real scenes through the real transition path and fails if any adjacent
 * pair violates, or if more than three violations occur within one second. */
#define _GNU_SOURCE
#include <cart/canvas.h>
#include <cart/scene.h>
#include <cart/transition.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LW 320
#define LH 180
#define FLASH_DELTA 0.10
#define DARK_LIMIT 0.80

static uint32_t buffer[LW * LH];

static double srgb_linear(double channel)
{
    channel /= 255.0;
    if (channel <= 0.04045)
        return channel / 12.92;
    return pow((channel + 0.055) / 1.055, 2.4);
}

static double relative_luminance(const uint32_t *pixels)
{
    double total = 0.0;

    for (int index = 0; index < LW * LH; index++) {
        uint32_t pixel = pixels[index];

        total += 0.2126 * srgb_linear(pixel & 0xffU)
               + 0.7152 * srgb_linear((pixel >> 8) & 0xffU)
               + 0.0722 * srgb_linear((pixel >> 16) & 0xffU);
    }
    return total / (double)(LW * LH);
}

static void fail(const char *message)
{
    fprintf(stderr, "FAIL %s\n", message);
    exit(1);
}

static void render_scene(uint32_t *destination, size_t which, uint64_t frame)
{
    struct cart_canvas canvas = {
        .pixels = destination, .width = LW, .height = LH, .stride = LW,
    };
    const struct cart_scene *scene = cart_scene_at(which);
    struct cart_scene_render_context context = {
        .canvas = &canvas, .row_start = 0, .row_end = LH,
        .frame = frame, .phase = CART_SCENE_RENDER_ROWS,
    };

    if (scene == NULL || scene->render == NULL)
        fail("scene lookup");
    scene->render(&context);
    context.phase = CART_SCENE_RENDER_OVERLAY;
    scene->render(&context);
}

/* Walk a full transition through the real transition module and assert
 * every adjacent frame pair stays under the flash threshold. */
static void check_transition(size_t from, size_t to)
{
    static uint32_t source[LW * LH];
    static uint32_t target[LW * LH];
    struct cart_transition transition;
    double previous = 0.0;
    int violations = 0;

    cart_transition_begin(&transition, from, to);
    for (uint64_t step = 0; step <= CART_TRANSITION_FRAMES; step++) {
        double luminance;

        render_scene(source, from, 240 + step);
        render_scene(target, to, 240 + step);
        cart_transition_blend(&transition, buffer, source, target,
                              LW * LH, step);
        luminance = relative_luminance(buffer);
        if (step > 0) {
            double delta = fabs(luminance - previous);
            double darker = luminance < previous ? luminance : previous;

            if (delta >= FLASH_DELTA && darker < DARK_LIMIT)
                violations++;
        }
        previous = luminance;
    }
    if (violations != 0) {
        fprintf(stderr, "transition %zu->%zu: %d flash pairs\n",
                from, to, violations);
        fail("transition exceeds WCAG general-flash threshold");
    }
    printf("  transition %zu->%zu clean\n", from, to);
}

/* Each scene in isolation must also be flash-free. */
static void check_scene_interior(size_t which)
{
    double previous = 0.0;
    int violations = 0;

    for (uint64_t frame = 200; frame < 290; frame++) {
        double luminance;

        render_scene(buffer, which, frame);
        luminance = relative_luminance(buffer);
        if (frame > 200) {
            double delta = fabs(luminance - previous);
            double darker = luminance < previous ? luminance : previous;

            if (delta >= FLASH_DELTA && darker < DARK_LIMIT)
                violations++;
        }
        previous = luminance;
    }
    if (violations > 3) {
        fprintf(stderr, "scene %zu: %d flash pairs in 3s\n", which, violations);
        fail("scene interior exceeds 3 flashes per second");
    }
    printf("  scene %zu interior clean (%d pairs)\n", which, violations);
}

int main(void)
{
    if (cart_scenes_init() != 0)
        fail("scene initialization");

    printf("scene interiors:\n");
    for (size_t index = 0; index < cart_scene_count(); index++)
        check_scene_interior(index);

    printf("transitions:\n");
    for (size_t index = 0; index < cart_scene_count(); index++)
        check_transition(index, (index + 1) % cart_scene_count());

    printf("PASS photosensitivity\n");
    return 0;
}
```

**Step 2: Wire it into the Makefile**

Add near the other test binary variables:

```make
PHOTOSENS_TEST_BIN := $(BUILD_DIR)/test-photosensitivity
TRANSITION_SRC := $(CART_DIR)/src/transition.c
```

Add the target:

```make
test-photosensitivity: $(PHOTOSENS_TEST_BIN)
	"$(PHOTOSENS_TEST_BIN)"

$(PHOTOSENS_TEST_BIN): $(CART_DIR)/tests/test_photosensitivity.c $(CANVAS_SRC) $(TRANSITION_SRC) $(SCENE_SRCS) FORCE
	@mkdir -p "$(BUILD_DIR)"
	"$(CC)" $(HOST_CFLAGS) "$(CART_DIR)/tests/test_photosensitivity.c" $(CANVAS_SRC) $(TRANSITION_SRC) $(SCENE_SRCS) $(HOST_LDFLAGS) -lm -o "$@"
```

Add `test-photosensitivity` to the `.PHONY` line and to the `test:` target dependency list.

**Step 3: Run to verify failure**

```sh
make test-photosensitivity
```

Expected: compile error — `cart/transition.h` not found. Correct RED state; Task 5 supplies it.

**Step 4: Commit**

```sh
git add cart/tests/test_photosensitivity.c cart/Makefile
git commit -m "test: add WCAG general-flash gate (RED)"
```

---

### Task 5: Implement the transition module

**Objective:** Provide a luminance-safe eased crossfade that makes Task 4 green.

**Files:**
- Create: `cart/include/cart/transition.h`
- Create: `cart/src/transition.c`

**Step 1: Create the header**

`cart/include/cart/transition.h`:

```c
#ifndef CART_TRANSITION_H
#define CART_TRANSITION_H

#include <stddef.h>
#include <stdint.h>

/* 15 frames at 30fps = 0.50s. Measured: holds the worst cut (2->3, hard-cut
 * delta 0.3473) to a max adjacent delta of 0.0411 — a 2.4x margin under the
 * 0.10 WCAG threshold. 6 frames passes but with no margin; do not shorten. */
#define CART_TRANSITION_FRAMES 15U

struct cart_transition {
    size_t from_scene;
    size_t to_scene;
    uint64_t elapsed_frames;
    int active;
};

void cart_transition_begin(struct cart_transition *transition,
                           size_t from_scene, size_t to_scene);
int cart_transition_active(const struct cart_transition *transition);
void cart_transition_advance(struct cart_transition *transition);

/* Blend weight in [0,256] for a given step, smoothstep-eased. */
unsigned int cart_transition_weight(uint64_t step);

void cart_transition_blend(const struct cart_transition *transition,
                           uint32_t *destination, const uint32_t *from,
                           const uint32_t *to, size_t count, uint64_t step);

#endif
```

**Step 2: Create the implementation**

`cart/src/transition.c`:

```c
#include <cart/transition.h>
#include <cart/canvas.h>

#include <string.h>

void cart_transition_begin(struct cart_transition *transition,
                           size_t from_scene, size_t to_scene)
{
    if (transition == NULL)
        return;
    transition->from_scene = from_scene;
    transition->to_scene = to_scene;
    transition->elapsed_frames = 0;
    transition->active = from_scene != to_scene;
}

int cart_transition_active(const struct cart_transition *transition)
{
    return transition != NULL && transition->active;
}

void cart_transition_advance(struct cart_transition *transition)
{
    if (transition == NULL || !transition->active)
        return;
    transition->elapsed_frames++;
    if (transition->elapsed_frames >= CART_TRANSITION_FRAMES)
        transition->active = 0;
}

/* Smoothstep 3t^2 - 2t^3 in fixed point, so the endpoints have zero
 * first derivative and neither end of the fade produces a step. */
unsigned int cart_transition_weight(uint64_t step)
{
    uint64_t numerator;
    uint64_t eased;

    if (step >= CART_TRANSITION_FRAMES)
        return 256U;
    /* t in [0,1] scaled by 1024 to keep the cube in 64-bit range. */
    numerator = step * 1024U / CART_TRANSITION_FRAMES;
    eased = (3U * numerator * numerator * 1024U -
             2U * numerator * numerator * numerator) / (1024U * 1024U);
    eased = eased * 256U / 1024U;
    if (eased > 256U)
        eased = 256U;
    return (unsigned int)eased;
}

void cart_transition_blend(const struct cart_transition *transition,
                           uint32_t *destination, const uint32_t *from,
                           const uint32_t *to, size_t count, uint64_t step)
{
    unsigned int amount;

    if (destination == NULL || from == NULL || to == NULL)
        return;
    (void)transition;
    amount = cart_transition_weight(step);
    if (amount == 0U) {
        memcpy(destination, from, count * sizeof(*destination));
        return;
    }
    if (amount >= 256U) {
        memcpy(destination, to, count * sizeof(*destination));
        return;
    }
    for (size_t index = 0; index < count; index++)
        destination[index] = cart_mix(from[index], to[index], amount);
}
```

**Step 3: Run the gate**

```sh
make test-photosensitivity
```

Expected output — every transition and interior reported clean, ending in `PASS photosensitivity`.

**Step 4: Sanity-check the eased weight curve is monotonic**

```sh
cd /home/cassie/projects/vita-wt/pstv-demo-cart-b5-input/cart
cat > /tmp/weightcheck.c <<'EOF'
#include <cart/transition.h>
#include <stdio.h>
int main(void){
    unsigned prev = 0;
    for (uint64_t s = 0; s <= CART_TRANSITION_FRAMES; s++) {
        unsigned w = cart_transition_weight(s);
        printf("step %2llu -> %3u %s\n", (unsigned long long)s, w,
               w < prev ? "** NON-MONOTONIC **" : "");
        prev = w;
    }
    return 0;
}
EOF
cc -std=c11 -Wall -Iinclude -o /tmp/weightcheck /tmp/weightcheck.c src/transition.c src/canvas.c
/tmp/weightcheck
```

Expected: weights rise monotonically from 0 to 256, no `NON-MONOTONIC` markers.

**Step 5: Commit**

```sh
git add cart/include/cart/transition.h cart/src/transition.c
git commit -m "feat: add luminance-safe eased scene crossfade"
```

---

### Task 6: Wire the transition into the render loop

**Objective:** Make the cart actually use the crossfade instead of hard-cutting.

**Files:**
- Modify: `cart/src/pstv-demo-cart.c`

**Step 1: Add the transition state and second low buffer**

After the existing `low` / `low_canvas` declarations:

```c
static uint32_t low_from[LW * LH];
static uint32_t low_to[LW * LH];
static struct cart_transition transition;
static size_t displayed_scene;
```

Add `#include <cart/transition.h>` to the include block.

**Step 2: Add a scene-targeted render helper**

`render_frame` currently hardcodes reading `runtime.scene_index`. Add a variant that renders a named scene into a named buffer:

```c
static void render_scene_into(uint32_t *destination, int which, uint64_t frame)
{
    uint32_t *saved = low_canvas.pixels;

    low_canvas.pixels = destination;
    render_frame(which, frame);
    low_canvas.pixels = saved;
}
```

This works because every render context binds `&low_canvas` at dispatch time inside `render_frame`.

**Step 3: Replace the render call in the main loop**

Replace this line in the `while (running)` body:

```c
        render_frame((int)runtime.scene_index, runtime.frame); upscale(fb); rendered_frames++;
```

with:

```c
        if (runtime.scene_index != displayed_scene &&
            !cart_transition_active(&transition)) {
            cart_transition_begin(&transition, displayed_scene,
                                  runtime.scene_index);
        }
        if (cart_transition_active(&transition)) {
            render_scene_into(low_from, (int)transition.from_scene,
                              runtime.frame);
            render_scene_into(low_to, (int)transition.to_scene,
                              runtime.frame);
            cart_transition_blend(&transition, low, low_from, low_to,
                                  LW * LH, transition.elapsed_frames);
            cart_transition_advance(&transition);
            if (!cart_transition_active(&transition))
                displayed_scene = transition.to_scene;
        } else {
            render_frame((int)displayed_scene, runtime.frame);
        }
        upscale(fb);
        rendered_frames++;
```

**Step 4: Initialize the transition state before the loop**

After `cart_runtime_init` succeeds:

```c
    displayed_scene = runtime.scene_index;
    memset(&transition, 0, sizeof(transition));
```

**Step 5: Verify the whole suite**

```sh
make test
```

Expected: all gates green, including `test-photosensitivity`.

**Step 6: Verify cost during a transition stays inside budget**

A crossfade renders **two** scenes per frame. Worst pair is scene 2 (1.387 ms) + scene 3 (0.540 ms) + blend + upscale. Measure it:

```sh
make host-capture && make capture
```

Expected: worst-case frame cost well under the 33.33 ms budget. If any scene pair exceeds ~25 ms, stop and report before proceeding — do not ship a transition that drops frames, since dropped frames during a fade reintroduce large luminance steps.

**Step 7: Commit**

```sh
git add cart/src/pstv-demo-cart.c
git commit -m "feat: render scene changes through the crossfade path"
```

---

### Task 7: Stage the blit through a back buffer

**Objective:** Shrink the tear window. Full page-flip is impossible on this driver (see Evidence), so reduce exposure instead of pretending to eliminate it.

**Files:**
- Modify: `cart/src/pstv-demo-cart.c`

**Step 1: Add the back buffer**

```c
static uint32_t back[FW * FH];
```

**Step 2: Split upscale from present**

Change `upscale(fb)` at the call site to:

```c
        upscale(back);
        memcpy(fb, back, (size_t)FW * FH * 4);
```

The upscale (scattered strided writes, ~0.108 ms host) now happens entirely in cached CPU memory. Only one linear `memcpy` touches uncached framebuffer memory, which is both faster and leaves a much shorter interval in which scanout can catch a partial frame.

**Step 3: Verify**

```sh
make test
```

Expected: all green.

**Step 4: Commit**

```sh
git add cart/src/pstv-demo-cart.c
git commit -m "fix: stage frame composition in a back buffer before the fb blit"
```

---

### Task 8: Document the constraint and update the skill

**Objective:** Record why tearing is not fully fixed, so nobody re-litigates it or assumes it was overlooked.

**Files:**
- Create: `cart/hardware/PHOTOSENSITIVITY.md`
- Modify: the `vita-linux-development` skill's incident reference

**Step 1: Write the hardware note**

`cart/hardware/PHOTOSENSITIVITY.md` must state:
- The three defects and their measured evidence (tables above).
- The chosen crossfade duration and *why 15 frames rather than 6*.
- That `CART_RUNTIME_MIN_CHANGE_NS` is a safety constant, not a tuning knob.
- That `simplefb` on PSTV exposes no `.fb_pan_display` and sets `yres_virtual = yres`, so page-flip/vsync is unavailable; the back buffer is a mitigation, not a cure.
- That full tear elimination requires a real Vita display driver — future work, not attempted here.

**Step 2: Patch the skill**

Update the incident reference in the `vita-linux-development` skill so the next agent finds the measured thresholds and the simplefb constraint without re-deriving them.

**Step 3: Commit**

```sh
git add cart/hardware/PHOTOSENSITIVITY.md
git commit -m "docs: record photosensitivity findings and simplefb constraint"
```

---

### Task 9: Full gate before any hardware consideration

**Objective:** Prove the whole thing green on host.

**Step 1: Run the complete gate**

```sh
cd /home/cassie/projects/vita-wt/pstv-demo-cart-b5-input/cart
make clean && make complete-test
```

Expected: every gate green, including `test-photosensitivity`.

**Step 2: Cross-build for ARM**

```sh
make cross-build && make cross-verify
```

Expected: `build/pstv-demo-cart.arm` produced and verified.

**Step 3: Report, and stop**

Summarize measured before/after luminance deltas and the cooldown behaviour. **Do not deploy to the PSTV.** Hardware validation is a separate decision that belongs to Cassie, and it needs a safe viewing protocol agreed first (see below).

---

## Hardware Re-Validation Protocol — Not Part of Implementation

When and if the cart returns to hardware, it should **not** be viewed directly first. Proposed order, for Cassie to accept or change:

1. Capture to video with the PSTV output recorded, nobody watching live.
2. Run the recorded capture through the same luminance analysis used in `test_photosensitivity.c` — the on-device result must match host predictions.
3. Only after the capture measures clean does anyone watch it.

This inverts the sequence that caused the original incident, where the failure mode was discovered by a person looking at it.

---

## Dependency Graph

```
Task 1 (rate-limit test RED)
    ↓
Task 2 (rate limiter)  ──────┐
    ↓                        │
Task 3 (auto rotation gated) │
                             │
Task 4 (photosens gate RED)  │
    ↓                        │
Task 5 (transition module)   │
    ↓                        │
Task 6 (wire into loop) ←────┘
    ↓
Task 7 (back buffer)
    ↓
Task 8 (docs)
    ↓
Task 9 (full gate, stop before hardware)
```

Tasks 1–3 and 4–5 are independent and may run in parallel; Task 6 requires both branches complete.

---

## Out of Scope — Explicitly Not Touched

Per the original brief, this plan does not modify:
- The kernel or any driver
- The rootfs
- BlueZ configuration or services
- The input engine (`input_evdev.c`, `input_actions.c`, `input_normalize.c`) — verified correct, deliberately left alone
- The worker pool (`worker_pool.c`) — verified correct

## Known Risk

The crossfade doubles per-frame render cost for 0.5s per transition. Host measurements say there is ample headroom (worst scene 4.2% of budget), but the PSTV's ARM CPU is far slower and the framebuffer write is the real unknown. Task 6 Step 6 exists specifically to catch this. **If frames drop during a fade, the fade itself becomes a flash hazard** — a dropped frame mid-fade is a large luminance step. That is the single most important thing to watch when this reaches hardware.
