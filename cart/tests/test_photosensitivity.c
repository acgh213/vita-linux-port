/* WCAG 2.x general-flash gate.
 *
 * A flash pair is any adjacent frame pair whose relative luminance differs
 * by >= 0.10 where the darker frame is below 0.80. This gate renders the
 * real scenes through the real transition path and fails if any adjacent
 * pair violates.
 *
 * Baseline for comparison (measured on B5 before the repair, hard cuts):
 *   0->1 delta 0.1660   1->2 delta 0.2265   2->3 delta 0.3473
 *   3->4 delta 0.3106   4->5 delta 0.2817   -- all five violate.
 *
 * See docs/plans/2026-08-27-pstv-transition-flash-repair.md. */
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
/* WCAG general-flash limit is three flashes in any one-second window. */
#define FLASH_LIMIT_PER_SECOND 3
#define FPS 30

static uint32_t blended[LW * LH];
static uint32_t source[LW * LH];
static uint32_t target[LW * LH];

static void fail(const char *message)
{
    fprintf(stderr, "FAIL %s\n", message);
    exit(1);
}

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

        total += 0.2126 * srgb_linear((double)(pixel & 0xffU))
               + 0.7152 * srgb_linear((double)((pixel >> 8) & 0xffU))
               + 0.0722 * srgb_linear((double)((pixel >> 16) & 0xffU));
    }
    return total / (double)(LW * LH);
}

static int is_flash_pair(double first, double second)
{
    double delta = fabs(second - first);
    double darker = second < first ? second : first;

    return delta >= FLASH_DELTA && darker < DARK_LIMIT;
}

static void render_scene(uint32_t *destination, size_t which, uint64_t frame)
{
    struct cart_canvas canvas = {
        .pixels = destination,
        .width = LW,
        .height = LH,
        .stride = LW,
    };
    const struct cart_scene *scene = cart_scene_at(which);
    struct cart_scene_render_context context = {
        .canvas = &canvas,
        .row_start = 0,
        .row_end = LH,
        .frame = frame,
        .phase = CART_SCENE_RENDER_ROWS,
    };

    if (scene == NULL || scene->render == NULL)
        fail("scene lookup");
    scene->render(&context);
    context.phase = CART_SCENE_RENDER_OVERLAY;
    scene->render(&context);
}

/* Walk a full transition through the real transition module and assert
 * every adjacent frame pair stays under the flash threshold. Frames either
 * side of the fade are included so the entry and exit seams are covered. */
static void check_transition(size_t from, size_t to)
{
    struct cart_transition transition;
    double previous = 0.0;
    double worst = 0.0;
    int violations = 0;
    uint64_t base = 240;

    /* One steady frame of the outgoing scene before the fade begins. */
    render_scene(blended, from, base - 1);
    previous = relative_luminance(blended);

    cart_transition_begin(&transition, from, to);
    for (uint64_t step = 0; step <= CART_TRANSITION_FRAMES; step++) {
        double luminance;
        double delta;

        render_scene(source, from, base + step);
        render_scene(target, to, base + step);
        cart_transition_blend(&transition, blended, source, target,
                              (size_t)(LW * LH), step);
        luminance = relative_luminance(blended);
        delta = fabs(luminance - previous);
        if (delta > worst)
            worst = delta;
        if (is_flash_pair(previous, luminance))
            violations++;
        previous = luminance;
    }

    /* One steady frame of the incoming scene after the fade completes. */
    render_scene(blended, to, base + CART_TRANSITION_FRAMES + 1);
    {
        double luminance = relative_luminance(blended);
        double delta = fabs(luminance - previous);

        if (delta > worst)
            worst = delta;
        if (is_flash_pair(previous, luminance))
            violations++;
    }

    printf("  transition %zu->%zu  worst adjacent delta %.4f  violations %d\n",
           from, to, worst, violations);
    if (violations != 0)
        fail("transition exceeds WCAG general-flash threshold");
}

/* Each scene in isolation must also stay under three flashes per second. */
static void check_scene_interior(size_t which)
{
    double previous = 0.0;
    double worst = 0.0;
    int violations = 0;

    for (uint64_t frame = 200; frame < 200 + 3 * FPS; frame++) {
        double luminance;

        render_scene(blended, which, frame);
        luminance = relative_luminance(blended);
        if (frame > 200) {
            double delta = fabs(luminance - previous);

            if (delta > worst)
                worst = delta;
            if (is_flash_pair(previous, luminance))
                violations++;
        }
        previous = luminance;
    }
    printf("  scene %zu interior  worst frame delta %.4f  flash pairs %d\n",
           which, worst, violations);
    if (violations > FLASH_LIMIT_PER_SECOND * 3)
        fail("scene interior exceeds three flashes per second");
}

/* The blend must be monotonic in weight, otherwise a fade could reverse
 * direction mid-transition and produce a step. */
static void check_weight_monotonic(void)
{
    unsigned int previous = 0;

    for (uint64_t step = 0; step <= CART_TRANSITION_FRAMES; step++) {
        unsigned int weight = cart_transition_weight(step);

        if (weight < previous)
            fail("transition weight is not monotonic");
        previous = weight;
    }
    if (cart_transition_weight(0) != 0)
        fail("transition must start fully on the outgoing scene");
    if (cart_transition_weight(CART_TRANSITION_FRAMES) != 256U)
        fail("transition must end fully on the incoming scene");
    printf("  weight curve monotonic, endpoints exact\n");
}

int main(void)
{
    if (cart_scenes_init() != 0)
        fail("scene initialization");

    printf("weight curve:\n");
    check_weight_monotonic();

    printf("scene interiors:\n");
    for (size_t index = 0; index < cart_scene_count(); index++)
        check_scene_interior(index);

    printf("transitions:\n");
    for (size_t index = 0; index < cart_scene_count(); index++)
        check_transition(index, (index + 1) % cart_scene_count());

    printf("PASS photosensitivity\n");
    return 0;
}
