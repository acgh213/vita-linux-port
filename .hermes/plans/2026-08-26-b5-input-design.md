# B5 Input Design — Discovery + Normalized Actions

Status: DESIGN (approved-by-evidence; replaces abandoned spike)
Authority: `.hermes/plans/2026-08-23_211206-pstv-demo-cart-toolkit.md` Task B5.
Evidence: `vita-linux-research/lab/b5-controller-exploration-2026-08-26.md`
(+ archived raw captures `b5-ds4-events*.bin`, failure trace `.btsnoop`),
skill refs `cart-b5-input-discovery.md`,
delegated research `~/.hermes/cache/delegation/subagent-summary-{1,2}-20260826_213610_777810.txt`.

## Goals

Discover compatible evdev nodes by capability/name (never `/dev/input/eventN`),
normalize their controls into `NEXT, PREVIOUS, SELECT, BACK, MENU` + analog
axes, survive disconnect/reconnect, stay nonfatal when input is absent, and run
the carousel from **both** the built-in Syscon pad and the Bluetooth DS4.

## Decisions (evidence-driven)

D1 **Multi-source, rank-ordered.** The plan says "nodes" plural and its
hardware acceptance requires Syscon to operate the carousel. On PSTV the Syscon
pad exists at boot and ranks above everything, so a single-device winner would
permanently starve the DS4 (tonight's newly-enabled transport). We therefore
open up to `CART_INPUT_MAX_SOURCES` (=4) ranked devices and merge their actions.
Ranking still determines poll priority; ties break to the lowest event index,
making results independent of `readdir()` order.

D2 **Authoritative probing after open.** Sysfs metadata only orders the scan.
After opening a node we classify it from `EVIOCGNAME/ID/BIT*` on the fd; any
mismatch/`ENODEV` mid-probe ⇒ skip. This kills the rejected spike's flattened
bitmap parser: ioctl buffers are ours; no sysfs word parsing remains.

D3 **Kernel HIDP is the transport** for Bluetooth pads (kernel
`BT_HIDP=y`, BlueZ `UserspaceHID=false`, `hid-playstation` binds). Userspace
doesn't care: a DS4 is just another ranked evdev source.

D4 **One normalization model for sticks and triggers:** both device families
report `0..255` centered ~128 (Syscon and DS4 measured identical tonight), but
we never assume that range — each axis is calibrated from that device's own
`EVIOCGABS` `input_absinfo`.

D5 **`QUIT` survives** beside the six plan actions (mapped to `BTN_MODE`/PS,
plus `KEY_ESC`/`KEY_Q` on keyboards): headless carts need a shutdown path that
is not only signals. Extension is additive; the six required actions exist
exactly.

## Device contract (captured 2026-08-26)

| | Syscon `event0` | DS4 v2 `event1` |
|---|---|---|
| identity | `PlayStation Vita Buttons (Syscon)` / phys `vita_syscon_buttons` | `Wireless Controller` / uniq MAC, bustype `0x0005` `054c:09cc` |
| buttons | faces 304-307(!), L1/R1 310/311, SEL/STA 314/315, PS 316, vol 114/115, power 116, d-pad 544-547 (`BTN_DPAD_UP/DOWN/LEFT/RIGHT`, per vita-buttons.c:56-59) | faces 304-307 + square 308 (`BTN_WEST`), L1/R1 310/311, L2d/R2d 312/313, Share/Options 314/315, PS 316, L3/R3 317/318 |
| sticks | `ABS_X/Y`, `ABS_RX/RY` 0..255 | same codes, 0..255 |
| extras | — | triggers `ABS_Z/RZ` 0..255, d-pad `HAT0X/Y` −1..1, motion/touch sibling nodes (skipped: not ranked) |

(307 = Triangle on both; Syscon has no Square/West code 308.)

## Action map

Every mapping accepts the union of both tables above; unknown codes ignored.

| Physical | Action |
|---|---|
| `BTN_SOUTH`(304) | `SELECT` |
| `BTN_EAST`(305) | `BACK` |
| `BTN_NORTH`(307) | `NEXT` |
| `BTN_WEST`(308, DS4) | `PREVIOUS` |
| `BTN_TL`(310)/`BTN_TR`(311) | `PREVIOUS`/`NEXT` (shoulder aliases) |
| d-pad RIGHT(+x, HAPPY?=code-side | hand over) `PREVIOUS`/`NEXT` (left/right only) |
| `BTN_SELECT`(314) *or* `BTN_START`(315) | `MENU` |
| `BTN_MODE`(316) / `KEY_ESC` / `KEY_Q` | `QUIT` |

Key repeats (`value==2`) are dropped before classification. Release (`0`)
never emits actions; it clears held state.

App-side consumption (runtime, unchanged ABI except one addition):
- `NEXT`/`PREVIOUS`: step `±1` with 8 s manual hold
  (**new**: `cart_runtime_request_previous()`, mirror of next; tested).
- `MENU`: toggle attract freeze — press 1 pins the current scene with 1 h hold;
  press 2 releases (attract resumes).
- `SELECT`: pin current scene 1 h; if frozen, stays pinned (acts as confirm).
- `BACK`: release freezes/pins (back to attract).
- Axes/sticks: exposed in state each frame; no gestures in v1 (tests cover
  normalization only).

## Normalization (pure function, exhaustive-tested)

For axis `a` with device `absinfo(min,max,flat)` and deadzone percent `dz`
(default 15, `0..50` validated):

```
span = max - min            // ≤ 0 ⇒ axis unusable, reports 0
raw  = clamp(value, min, max)
dz_n = (span * dz) / 100    // integer floor
center_of_raw…       // see below: hinge at midpoint c = min + span/2
if raw in [c-dz_n, c+dz_n] ⇒ 0
else negative side:  out = ((raw - (c+dz_n)) * 100) / (max-(c+dz_n))   // ≤ -1
     positive side:  out = ((raw - (c-dz_n)) * 100) / ((c-dz_n)-min)
```

Rounding toward zero (integer division truncation) — boundary tests hit
exactly `−100`, `0`, `+100`, both deadzone edges, and clamped overflow inputs.
Triggers (`min==0`, one-sided, DS4 `ABS_Z/RZ`) get the same math ⇒ natural
`0..+100`. Hats (−1..1) land on `−100/0/+100` exactly.

## Per-source lifecycle (state machine)

```
ABSENT ──ranked scan + authority probe──▶ CONNECTED
CONNECTED --EOF/ENODEV/EIO--> CLOSED(with cleanup) ──rescan──▶ ABSENT
CONNECTED --SYN_DROPPED--> DESYNC (events discarded) --SYN_REPORT--▶ CONNECTED
                                     (state resynced via EVIOCGKEY/EVIOCGABS)
```

- Open `O_RDONLY|O_NONBLOCK|O_CLOEXEC`.
- Generation counter per source bumps on every connect/disconnect;
  aggregated state carries the total generation so consumers detect churn.
- Rescan cadence: every 2 s while any slot is empty, driven off the normal
  tick (no threads).
- Disconnect clears that source's held keys/axes contribution and is never
  duplicated; reconnect does not leak fds (single ownership, central close).

## Public API (replaces spike header wholesale)

```c
#define CART_INPUT_MAX_SOURCES 4
enum cart_input_action {
    CART_INPUT_NONE=0, CART_INPUT_NEXT, CART_INPUT_PREVIOUS,
    CART_INPUT_SELECT, CART_INPUT_BACK, CART_INPUT_MENU,
    CART_INPUT_QUIT,
};
struct cart_input_source { int fd; int rank; uint32_t generation;
    char path[..]; char name[..];
    /* calibration snapshot */ struct { int32_t min,max,flat; uint8_t ok; } axis[CART_INPUT_AXIS_COUNT]; };
struct cart_input_frame {
    uint32_t pressed_actions;        // bitmask of enum (1<<action)
    uint32_t held_keys;              // debug/debug-level bitmask (source-local)
    int16_t axis[CART_INPUT_AXIS_COUNT];   // LX LY RX RY L2t R2t HX HY → ±100
    uint32_t connected_mask;         // bit per slot
    uint32_t generation_total;
};
/* discovery & lifecycle */
int  cart_input_init(struct cart_input*, const char *sys_class_input, const char *dev_input);
void cart_input_rescan(struct cart_input*);           // called by poll when due
void cart_input_shutdown(struct cart_input*);
/* per-tick: drains fds, returns queued actions one call at a time */
int  cart_input_poll(struct cart_input*, enum cart_input_action *action,
                     struct cart_input_frame *frame);
```

(`CART_INPUT_QUIT` removed from spike public enum spikes nothing: name reused.)
Sysfs text parsing: none. Ranking uses injected stat data + post-open ioctls.

## File layout (plan-specified names)

- `cart/include/cart/input.h` — new API (above).
- `cart/src/input_evdev.c` — probe/discover/lifecycle/poll.
- `cart/src/input_normalize.c` — pure normalization + edge decoding.
- `cart/tests/test_input.c` — full matrix below.
- `cart/src/runtime.c` + `include/cart/runtime.h` — add `request_previous`.
- `cart/src/pstv-demo-cart.c` — consume the new action set.
- `cart/Makefile`, `cart/README.md`.

Spike files `input.c` deleted; findings preserved in git history + skill note.

## Test matrix (RED first, one behavior each)

Pure: bitmap-independent normalization edges (all D4-boundary cases ×3 range
shapes); deadzone validation rejects `dz>50`; hat exactness; trigger shape.
Edge decoder: known-code table covers every row of the action map; repeat
suppression; release-clears-held.
Lifecycle (pipe-backed fd fixtures): partial read of 7 bytes ⇒ buffered;
multiple events in one read; `EAGAIN` clean; `EINTR`; EOF ⇒ one disconnect +
slot reusable after simulated re-open (generation bump proves reuse, second
close leaks nothing via lsouls-style fd bookkeeping asserts); `SYN_DROPPED`
swallows until `SYN_REPORT`, then `EVIOCGKEY` resync observable via held-mask;
candidate-open-failure (ENOENT device node) skipped gracefully with others
still found; broken unrelated node never poisons; ranking ties and priority
(phys > name > gamepad-caps > keyboard), `readdir`-order independent
(two shuffled fixtures, same winner set); absent devices ⇒ init returns 0,
poll returns NONE forever, no crash; zombie-slot recovery after ENODEV.
Integration with runtime: request_previous mirrors next; menu freeze/
unfreeze semantics; SELECT/BACK pins.
No test requires real hardware. Live capture binaries remain golden evidence
but decode through the same edge decoder in a fixture replaying recorded
frames offline.

## Risks / out-of-scope

- PS button on DS4 unobserved live (316 declared, never emitted tonight);
  mapper handles it, hardware check deferred to carousel gate.
- Long-HOLD-to-repeat, menus inside scenes, motion/touchpad consumption:
  post-B5.
- 911b AMP suppression: separate kernel task, blocks production release, not
  the B5 software gate (documented in lab record).
