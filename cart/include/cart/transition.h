#ifndef CART_TRANSITION_H
#define CART_TRANSITION_H

#include <stddef.h>
#include <stdint.h>

/* 15 frames at 30fps = 0.50s.
 *
 * Measured against the real scenes: holds the worst cut (2->3, whose hard-cut
 * luminance delta was 0.3473) to a maximum adjacent-frame delta of 0.0411 —
 * a 2.4x margin under the 0.10 WCAG general-flash threshold. A 6-frame fade
 * also passes but at 0.0948, only 5% under the line with no headroom for
 * scene changes. Do not shorten this without re-running
 * `make test-photosensitivity`.
 *
 * See docs/plans/2026-08-27-pstv-transition-flash-repair.md. */
#define CART_TRANSITION_FRAMES UINT64_C(15)

/* Blend weight range accepted by cart_mix: 0 = fully outgoing scene,
 * 256 = fully incoming scene. */
#define CART_TRANSITION_WEIGHT_MAX 256U

struct cart_transition {
    size_t from_scene;
    size_t to_scene;
    uint64_t elapsed_frames;
    int active;
    int sources_ready;
};

/* Begin a fade from one scene to another. A transition to the same scene is
 * inert (never becomes active). */
void cart_transition_begin(struct cart_transition *transition,
                           size_t from_scene, size_t to_scene);

int cart_transition_active(const struct cart_transition *transition);

/* Advance one frame; clears the active flag once the fade completes. */
void cart_transition_advance(struct cart_transition *transition);

/* Transition endpoint frames are sampled once and reused for the fade. */
int cart_transition_sources_need_render(const struct cart_transition *transition);
void cart_transition_mark_sources_rendered(struct cart_transition *transition);

/* Smoothstep-eased blend weight in [0, 256] for the given step. Monotonic,
 * exact at both endpoints. */
unsigned int cart_transition_weight(uint64_t step);

/* Blend `count` pixels from `from` toward `to` at the eased weight for
 * `step`, writing into `destination`. */
void cart_transition_blend(const struct cart_transition *transition,
                           uint32_t *destination, const uint32_t *from,
                           const uint32_t *to, size_t count, uint64_t step);

#endif
