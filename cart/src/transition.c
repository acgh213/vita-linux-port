#include <cart/transition.h>
#include <cart/canvas.h>

#include <string.h>

/* Fixed-point scale for the eased curve. 1024 keeps t^3 well inside 64-bit
 * range (1024^3 = 2^30) while giving far more resolution than the 8-bit
 * output weight needs, so rounding never loses a step. */
#define FIXED_ONE UINT64_C(1024)

void cart_transition_begin(struct cart_transition *transition,
                           size_t from_scene, size_t to_scene)
{
    if (transition == NULL)
        return;
    transition->from_scene = from_scene;
    transition->to_scene = to_scene;
    transition->elapsed_frames = 0;
    transition->active = from_scene != to_scene;
    transition->sources_ready = 0;
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

int cart_transition_sources_need_render(const struct cart_transition *transition)
{
    return transition != NULL && transition->active && !transition->sources_ready;
}

void cart_transition_mark_sources_rendered(struct cart_transition *transition)
{
    if (transition != NULL && transition->active)
        transition->sources_ready = 1;
}

/* Smoothstep: 3t^2 - 2t^3, evaluated in fixed point.
 *
 * Chosen over a linear ramp because its first derivative is zero at both
 * endpoints, so neither the entry nor the exit of the fade produces a
 * visible step against the steady frames either side of it. */
unsigned int cart_transition_weight(uint64_t step)
{
    uint64_t t;
    uint64_t eased;

    if (step == 0)
        return 0U;
    if (step >= CART_TRANSITION_FRAMES)
        return CART_TRANSITION_WEIGHT_MAX;

    /* t in [0, FIXED_ONE] */
    t = step * FIXED_ONE / CART_TRANSITION_FRAMES;

    /* 3t^2 - 2t^3, descaled back to [0, FIXED_ONE]. Both terms are
     * computed at t^3 scale before subtraction so the intermediate
     * result never underflows. */
    eased = (UINT64_C(3) * t * t * FIXED_ONE - UINT64_C(2) * t * t * t) /
            (FIXED_ONE * FIXED_ONE);

    /* Descale to the blend weight range, rounding to nearest. */
    eased = (eased * CART_TRANSITION_WEIGHT_MAX + FIXED_ONE / 2) / FIXED_ONE;
    if (eased > CART_TRANSITION_WEIGHT_MAX)
        eased = CART_TRANSITION_WEIGHT_MAX;
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
    if (amount >= CART_TRANSITION_WEIGHT_MAX) {
        memcpy(destination, to, count * sizeof(*destination));
        return;
    }
    for (size_t index = 0; index < count; index++)
        destination[index] = cart_mix(from[index], to[index], amount);
}
