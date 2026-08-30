#ifndef CART_RUNTIME_H
#define CART_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

/* Photosensitivity guard: minimum interval between committed scene changes.
 * WCAG 2.x general-flash limit is 3 flashes/sec; 500 ms gives 2/sec with
 * margin. Passed to cart_runtime_init as min_change_ns. Tests may pass 0 to
 * exercise pure stepping semantics.
 * See docs/plans/2026-08-27-pstv-transition-flash-repair.md. */
#define CART_RUNTIME_MIN_CHANGE_NS UINT64_C(500000000)

struct cart_runtime {
    size_t scene_count;
    size_t scene_index;
    size_t manual_scene_index;
    uint64_t origin_ns;
    uint64_t frame_period_ns;
    uint64_t scene_period_ns;
    uint64_t manual_hold_until_ns;
    uint64_t min_change_ns;
    uint64_t last_change_ns;
    int had_change;
    uint64_t next_deadline_ns;
    uint64_t frame;
    uint64_t dropped_deadlines;
    uint64_t sleep_ns;
    int manual_hold_active;
};

int cart_runtime_init(struct cart_runtime *runtime, size_t scene_count,
                      uint64_t origin_ns, uint64_t frame_period_ns,
                      uint64_t scene_period_ns, uint64_t min_change_ns);
int cart_runtime_tick(struct cart_runtime *runtime, uint64_t now_ns);
void cart_runtime_request_next(struct cart_runtime *runtime, uint64_t now_ns,
                               uint64_t hold_ns);
void cart_runtime_request_previous(struct cart_runtime *runtime, uint64_t now_ns,
                                   uint64_t hold_ns);

#endif
