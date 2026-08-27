#include <cart/runtime.h>

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static uint64_t add_saturating(uint64_t left, uint64_t right)
{
    if (left > UINT64_MAX - right)
        return UINT64_MAX;
    return left + right;
}

int cart_runtime_init(struct cart_runtime *runtime, size_t scene_count,
                      uint64_t origin_ns, uint64_t frame_period_ns,
                      uint64_t scene_period_ns)
{
    if (runtime == NULL || scene_count == 0 || frame_period_ns == 0 ||
        scene_period_ns == 0 || origin_ns >= UINT64_MAX - frame_period_ns)
        return -1;

    memset(runtime, 0, sizeof(*runtime));
    runtime->scene_count = scene_count;
    runtime->origin_ns = origin_ns;
    runtime->frame_period_ns = frame_period_ns;
    runtime->scene_period_ns = scene_period_ns;
    runtime->next_deadline_ns = origin_ns + frame_period_ns;
    runtime->sleep_ns = frame_period_ns;
    return 0;
}

int cart_runtime_tick(struct cart_runtime *runtime, uint64_t now_ns)
{
    uint64_t elapsed_ns;
    uint64_t late_ns;

    if (runtime == NULL || runtime->scene_count == 0 ||
        runtime->frame_period_ns == 0 || runtime->scene_period_ns == 0)
        return -1;
    if (now_ns < runtime->origin_ns)
        now_ns = runtime->origin_ns;

    elapsed_ns = now_ns - runtime->origin_ns;
    runtime->frame = elapsed_ns / runtime->frame_period_ns;
    if (runtime->manual_hold_active && now_ns >= runtime->manual_hold_until_ns)
        runtime->manual_hold_active = 0;
    if (runtime->manual_hold_active) {
        runtime->scene_index = runtime->manual_scene_index;
    } else {
        runtime->scene_index = (size_t)((elapsed_ns / runtime->scene_period_ns) %
                                        runtime->scene_count);
    }

    if (now_ns < runtime->next_deadline_ns) {
        runtime->sleep_ns = runtime->next_deadline_ns - now_ns;
        return 0;
    }
    if (now_ns > UINT64_MAX - runtime->frame_period_ns)
        return -1;

    late_ns = now_ns - runtime->next_deadline_ns;
    if (late_ns < runtime->frame_period_ns) {
        if (runtime->next_deadline_ns > UINT64_MAX - runtime->frame_period_ns)
            return -1;
        runtime->next_deadline_ns += runtime->frame_period_ns;
    } else {
        runtime->dropped_deadlines += late_ns / runtime->frame_period_ns;
        runtime->next_deadline_ns = now_ns + runtime->frame_period_ns;
    }
    runtime->sleep_ns = runtime->next_deadline_ns - now_ns;
    return 0;
}

void cart_runtime_request_next(struct cart_runtime *runtime, uint64_t now_ns,
                               uint64_t hold_ns)
{
    if (runtime == NULL || runtime->scene_count == 0)
        return;

    runtime->manual_scene_index = (runtime->scene_index + 1) % runtime->scene_count;
    runtime->scene_index = runtime->manual_scene_index;
    if (hold_ns == 0) {
        runtime->manual_hold_active = 0;
        return;
    }
    runtime->manual_hold_until_ns = add_saturating(now_ns, hold_ns);
    runtime->manual_hold_active = 1;
}

void cart_runtime_request_previous(struct cart_runtime *runtime, uint64_t now_ns,
                                   uint64_t hold_ns)
{
    if (runtime == NULL || runtime->scene_count == 0)
        return;

    runtime->manual_scene_index = (runtime->scene_index + runtime->scene_count - 1) %
                                  runtime->scene_count;
    runtime->scene_index = runtime->manual_scene_index;
    if (hold_ns == 0) {
        runtime->manual_hold_active = 0;
        return;
    }
    runtime->manual_hold_until_ns = add_saturating(now_ns, hold_ns);
    runtime->manual_hold_active = 1;
}
