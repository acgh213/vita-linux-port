#include <cart/runtime.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#define START UINT64_C(1000)
#define STEP UINT64_C(100)
#define SCENE_PERIOD UINT64_C(800)

static void fail(const char *message)
{
    fprintf(stderr, "FAIL %s\n", message);
    exit(1);
}

static void expect(int condition, const char *message)
{
    if (!condition)
        fail(message);
}

static struct cart_runtime fresh_runtime(void)
{
    struct cart_runtime runtime;

    expect(cart_runtime_init(&runtime, 6, START, STEP, SCENE_PERIOD) == 0,
           "runtime initialization");
    return runtime;
}

static void test_fixed_step_progression(void)
{
    struct cart_runtime runtime = fresh_runtime();

    cart_runtime_tick(&runtime, START);
    expect(runtime.frame == 0, "origin starts at frame zero");
    expect(runtime.scene_index == 0, "origin starts at first scene");
    expect(runtime.sleep_ns == STEP, "origin sleeps one full period");

    cart_runtime_tick(&runtime, START + STEP - 1);
    expect(runtime.frame == 0, "frame does not advance early");
    expect(runtime.sleep_ns == 1, "sub-period sleep is exact");

    cart_runtime_tick(&runtime, START + STEP);
    expect(runtime.frame == 1, "frame advances at fixed-step boundary");
    expect(runtime.next_deadline_ns == START + 2 * STEP,
           "on-time deadline advances one period");
    expect(runtime.sleep_ns == STEP, "on-time frame gets a full period");
}

static void test_scene_timeout_and_manual_hold(void)
{
    struct cart_runtime runtime = fresh_runtime();

    cart_runtime_tick(&runtime, START + SCENE_PERIOD);
    expect(runtime.scene_index == 1, "automatic scene uses elapsed scene period");

    cart_runtime_request_next(&runtime, START + SCENE_PERIOD,
                              2 * SCENE_PERIOD);
    expect(runtime.scene_index == 2, "manual next starts after automatic scene");
    expect(runtime.manual_hold_active, "manual selection enables hold");

    cart_runtime_tick(&runtime, START + 3 * SCENE_PERIOD - 1);
    expect(runtime.scene_index == 2, "manual scene holds across auto scene boundary");
    expect(runtime.manual_hold_active, "hold remains active before deadline");

    cart_runtime_tick(&runtime, START + 3 * SCENE_PERIOD);
    expect(runtime.scene_index == 3, "automatic scene is recomputed at hold timeout");
    expect(!runtime.manual_hold_active, "manual hold expires exactly at deadline");
}

static void test_request_previous_wraps_to_last_scene(void)
{
    struct cart_runtime runtime = fresh_runtime();

    cart_runtime_tick(&runtime, START);
    expect(runtime.scene_index == 0, "origin starts at first scene");
    cart_runtime_request_previous(&runtime, START, 0);
    expect(runtime.scene_index == 5, "previous from first scene wraps to last");
    expect(runtime.manual_scene_index == 5,
           "previous records the manual scene index");
}

static void test_request_previous_steps_backward(void)
{
    struct cart_runtime runtime = fresh_runtime();

    cart_runtime_tick(&runtime, START + 3 * SCENE_PERIOD);
    expect(runtime.scene_index == 3, "automatic scene advances by period");
    cart_runtime_request_previous(&runtime, START + 3 * SCENE_PERIOD, 0);
    expect(runtime.scene_index == 2, "previous steps back one scene");
    cart_runtime_request_previous(&runtime, START + 3 * SCENE_PERIOD, 0);
    expect(runtime.scene_index == 1, "previous steps back another scene");
}

static void test_request_previous_hold_pins_like_next(void)
{
    struct cart_runtime runtime = fresh_runtime();

    cart_runtime_tick(&runtime, START + 2 * SCENE_PERIOD);
    expect(runtime.scene_index == 2, "automatic scene reaches index two");
    cart_runtime_request_previous(&runtime, START + 2 * SCENE_PERIOD,
                                  2 * SCENE_PERIOD);
    expect(runtime.scene_index == 1, "manual previous steps back one scene");
    expect(runtime.manual_hold_active, "manual previous enables hold");

    cart_runtime_tick(&runtime, START + 4 * SCENE_PERIOD - 1);
    expect(runtime.scene_index == 1, "manual previous holds across auto boundary");
    expect(runtime.manual_hold_active, "previous hold remains active before deadline");

    cart_runtime_tick(&runtime, START + 4 * SCENE_PERIOD);
    expect(runtime.scene_index == 4, "automatic scene is recomputed at hold timeout");
    expect(!runtime.manual_hold_active, "previous hold expires exactly at deadline");
}

static void test_request_previous_without_hold_does_not_pin(void)
{
    struct cart_runtime runtime = fresh_runtime();

    cart_runtime_tick(&runtime, START + 3 * SCENE_PERIOD);
    cart_runtime_request_previous(&runtime, START + 3 * SCENE_PERIOD, 0);
    expect(runtime.scene_index == 2, "previous without hold still steps back");
    expect(!runtime.manual_hold_active, "previous without hold does not pin");

    cart_runtime_tick(&runtime, START + 3 * SCENE_PERIOD + STEP);
    expect(runtime.scene_index == 3, "attract resumes on the next tick");
}

static void test_request_previous_safety(void)
{
    struct cart_runtime runtime;

    cart_runtime_request_previous(NULL, START, SCENE_PERIOD);

    expect(cart_runtime_init(&runtime, 1, START, STEP, SCENE_PERIOD) == 0,
           "single scene runtime initialization");
    cart_runtime_tick(&runtime, START + 2 * SCENE_PERIOD);
    cart_runtime_request_previous(&runtime, START + 2 * SCENE_PERIOD,
                                  SCENE_PERIOD);
    expect(runtime.scene_index == 0, "previous on single scene stays at zero");
    expect(runtime.manual_hold_active, "single scene previous still honors hold");
}

static void test_one_frame_overrun_keeps_schedule(void)
{
    struct cart_runtime runtime = fresh_runtime();

    cart_runtime_tick(&runtime, START + STEP + STEP / 2);
    expect(runtime.frame == 1, "one-frame overrun keeps elapsed frame number");
    expect(runtime.dropped_deadlines == 0, "one-frame overrun drops no deadlines");
    expect(runtime.next_deadline_ns == START + 2 * STEP,
           "one-frame overrun keeps original cadence");
    expect(runtime.sleep_ns == STEP / 2, "one-frame overrun sleeps remaining cadence");
}

static void test_multi_frame_overrun_resynchronizes(void)
{
    struct cart_runtime runtime = fresh_runtime();

    cart_runtime_tick(&runtime, START + 5 * STEP);
    expect(runtime.frame == 5, "multi-frame overrun tracks elapsed frame number");
    expect(runtime.dropped_deadlines == 4, "multi-frame overrun records skipped deadlines");
    expect(runtime.next_deadline_ns == START + 6 * STEP,
           "multi-frame overrun schedules from now without catch-up storm");
    expect(runtime.sleep_ns == STEP, "resynchronized frame sleeps one full period");
}

static void test_no_negative_sleep_or_invalid_configuration(void)
{
    struct cart_runtime runtime;

    expect(cart_runtime_init(NULL, 6, START, STEP, SCENE_PERIOD) != 0,
           "null runtime rejected");
    expect(cart_runtime_init(&runtime, 0, START, STEP, SCENE_PERIOD) != 0,
           "zero scene count rejected");
    expect(cart_runtime_init(&runtime, 6, START, 0, SCENE_PERIOD) != 0,
           "zero frame period rejected");
    expect(cart_runtime_init(&runtime, 6, START, STEP, 0) != 0,
           "zero scene period rejected");
    expect(cart_runtime_init(&runtime, 6, UINT64_MAX - 2 * STEP, STEP,
                             SCENE_PERIOD) == 0,
           "origin with one representable deadline accepted");
    expect(cart_runtime_tick(&runtime, UINT64_MAX - STEP) == 0,
           "last representable deadline is usable");
    expect(cart_runtime_tick(&runtime, UINT64_MAX) != 0,
           "unrepresentable follow-up deadline stops runtime");

    runtime = fresh_runtime();
    cart_runtime_tick(&runtime, START + 1000 * STEP);
    expect(runtime.sleep_ns > 0 && runtime.sleep_ns <= STEP,
           "successful tick always returns a positive bounded sleep");
}

int main(void)
{
    test_fixed_step_progression();
    test_scene_timeout_and_manual_hold();
    test_one_frame_overrun_keeps_schedule();
    test_multi_frame_overrun_resynchronizes();
    test_no_negative_sleep_or_invalid_configuration();
    test_request_previous_wraps_to_last_scene();
    test_request_previous_steps_backward();
    test_request_previous_hold_pins_like_next();
    test_request_previous_without_hold_does_not_pin();
    test_request_previous_safety();
    printf("all runtime tests passed\n");
    return 0;
}
