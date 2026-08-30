#include <cart/input_normalize.h>

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

#define CHECK_EQ(got, want) do { \
    if ((got) != (want)) { \
        fprintf(stderr, "FAIL %s:%d: %s == %ld, want %ld\n", __FILE__, \
                __LINE__, #got, (long)(got), (long)(want)); \
        exit(1); \
    } \
} while (0)

/* Spec formula (design doc "Normalization"), truncation toward zero:
 *   zero zone:      [c-n .. c+n]          -> 0
 *   negative side:  ((raw - lo) * 100) / (lo - min)    -> [-100..-1]
 *   positive side:  ((raw - hi) * 100) / (max - hi)    -> [1..100]
 * with lo = c-n, hi = c+n, c = min + span/2, n = span*dz/200 (all floor),
 * raw clamped to [min,max] first.
 *
 * Documented consequence on wide-band shapes: the FIRST raw step beyond
 * the deadzone may truncate back to 0 (e.g. stick 0..255 dz15 band width
 * 109 > 100). This is the classic truncating joystick behavior — full
 * throw still reaches +-100 exactly — and the explicit zero-at-first-step
 * assertions below pin that behavior down deliberately. */

static struct cart_input_axis_range stick_dz15(void)
{
    return (struct cart_input_axis_range) {
        .min = 0, .max = 255, .deadzone_pct = 15,
    };
}

/* Stick shape 0..255 dz15: span=255, c=127, n=19.
 * Zero band [108..146]; negative denominator lo-min = 108; positive
 * denominator max-hi = 109. */
static void test_stick_shape_deadzone_15(void)
{
    struct cart_input_axis_range range = stick_dz15();

    /* Deadzone proper. */
    CHECK_EQ(cart_input_axis_normalize(108, &range), 0);
    CHECK_EQ(cart_input_axis_normalize(127, &range), 0);
    CHECK_EQ(cart_input_axis_normalize(146, &range), 0);

    /* First steps beyond the rims truncate to zero on this shape —
     * pinned intentionally (see header comment). */
    CHECK_EQ(cart_input_axis_normalize(147, &range), 0);
    CHECK_EQ(cart_input_axis_normalize(107, &range), 0);

    /* Then the classical staircase begins. */
    CHECK_EQ(cart_input_axis_normalize(148, &range), 1);   /* 200/109 */
    CHECK_EQ(cart_input_axis_normalize(149, &range), 2);   /* 300/109 */
    CHECK_EQ(cart_input_axis_normalize(150, &range), 3);   /* 400/109 */
    CHECK_EQ(cart_input_axis_normalize(106, &range), -1);  /* -200/108 */
    CHECK_EQ(cart_input_axis_normalize(105, &range), -2);
    CHECK_EQ(cart_input_axis_normalize(104, &range), -3);

    /* Mid-travel spots, both directions. */
    CHECK_EQ(cart_input_axis_normalize(160, &range), 12);  /* 1400/109 */
    CHECK_EQ(cart_input_axis_normalize(54, &range), -50);  /* 5400/108 */

    /* Full throw lands exactly on +-100; one short does not overshoot. */
    CHECK_EQ(cart_input_axis_normalize(255, &range), 100);
    CHECK_EQ(cart_input_axis_normalize(254, &range), 99);
    CHECK_EQ(cart_input_axis_normalize(0, &range), -100);
    CHECK_EQ(cart_input_axis_normalize(1, &range), -99);

    /* Clamping both sides. */
    CHECK_EQ(cart_input_axis_normalize(300, &range), 100);
    CHECK_EQ(cart_input_axis_normalize(-5, &range), -100);

    /* Monotonicity across the whole positive band (sampled). */
    {
        int32_t raw;
        long prev = 0;

        for (raw = 146; raw <= 255; raw++) {
            long got = cart_input_axis_normalize(raw, &range);

            CHECK(got >= prev);
            prev = got;
        }
        CHECK_EQ(prev, 100);
    }
}

/* Trigger shape 0..255 dz15 shares identical math; spot-exact at 160. */
static void test_trigger_shape_identical_math(void)
{
    struct cart_input_axis_range range = stick_dz15();

    CHECK_EQ(cart_input_axis_normalize(160, &range), 12);
    CHECK_EQ(cart_input_axis_normalize(254, &range), 99);
}

/* Hat shape -1..1 dz15: n=0, zero band collapses to {0}; denominators 1. */
static void test_hat_shape_exactness(void)
{
    struct cart_input_axis_range range = {
        .min = -1, .max = 1, .deadzone_pct = 15,
    };

    CHECK_EQ(cart_input_axis_normalize(-1, &range), -100);
    CHECK_EQ(cart_input_axis_normalize(0, &range), 0);
    CHECK_EQ(cart_input_axis_normalize(1, &range), 100);
    CHECK_EQ(cart_input_axis_normalize(-9, &range), -100);
    CHECK_EQ(cart_input_axis_normalize(9, &range), 100);
}

/* Even span variant: 0..256 dz15 -> c=128, n=19, zero band [109..147],
 * denominators 109 BOTH sides — fully symmetric shape, so the staircase
 * lands evenly and mirror inputs give mirrored outputs. */
static void test_even_span_variant(void)
{
    struct cart_input_axis_range range = {
        .min = 0, .max = 256, .deadzone_pct = 15,
    };

    CHECK_EQ(cart_input_axis_normalize(109, &range), 0);
    CHECK_EQ(cart_input_axis_normalize(147, &range), 0);
    /* First step past either rim still truncates to 0 (denominator 109),
     * exactly like the odd live shape — pinned deliberately. */
    CHECK_EQ(cart_input_axis_normalize(148, &range), 0);
    CHECK_EQ(cart_input_axis_normalize(108, &range), 0);
    /* Staircase starts one step later, perfectly mirrored. */
    CHECK_EQ(cart_input_axis_normalize(149, &range), 1);    /* 200/109 */
    CHECK_EQ(cart_input_axis_normalize(107, &range), -1);   /* -200/109 */
    CHECK_EQ(cart_input_axis_normalize(255, &range), 99);   /* 10800/109 */
    CHECK_EQ(cart_input_axis_normalize(1, &range), -99);    /* -10800/109 */
    CHECK_EQ(cart_input_axis_normalize(256, &range), 100);
    CHECK_EQ(cart_input_axis_normalize(0, &range), -100);
}

/* dz=0: zero only at the exact center; neighbors truncate to 0 too on
 * this shape (band width 127/128 > 100) — pinned, then the staircase. */
static void test_zero_deadzone_center_only(void)
{
    struct cart_input_axis_range range = {
        .min = 0, .max = 255, .deadzone_pct = 0,
    };

    CHECK_EQ(cart_input_axis_normalize(127, &range), 0);
    CHECK_EQ(cart_input_axis_normalize(126, &range), 0);   /* 100/127 */
    CHECK_EQ(cart_input_axis_normalize(128, &range), 0);   /* 100/128 */
    CHECK_EQ(cart_input_axis_normalize(125, &range), -1);  /* -200/127 */
    CHECK_EQ(cart_input_axis_normalize(64, &range), -49);  /* 6300/127 */
    CHECK_EQ(cart_input_axis_normalize(192, &range), 50);  /* 6500/128 */
}

/* dz=50: wide plateau [64..190]; rims already produce +-1 because band
 * widths (64/65) sit below 100. */
static void test_half_deadzone_wide_plateau(void)
{
    struct cart_input_axis_range range = {
        .min = 0, .max = 255, .deadzone_pct = 50,
    };

    CHECK_EQ(cart_input_axis_normalize(64, &range), 0);
    CHECK_EQ(cart_input_axis_normalize(190, &range), 0);
    CHECK_EQ(cart_input_axis_normalize(63, &range), -1);   /* 100/64 */
    CHECK_EQ(cart_input_axis_normalize(191, &range), 1);   /* 100/65 */
    CHECK_EQ(cart_input_axis_normalize(0, &range), -100);
    CHECK_EQ(cart_input_axis_normalize(255, &range), 100);
}

/* Invalid inputs reject rather than guess. */
static void test_invalid_inputs_rejected(void)
{
    struct cart_input_axis_range bad_min_max = {
        .min = 200, .max = 100, .deadzone_pct = 15,
    };
    struct cart_input_axis_range flat = {
        .min = 5, .max = 5, .deadzone_pct = 15,
    };
    struct cart_input_axis_range over = {
        .min = 0, .max = 255, .deadzone_pct = 51,
    };
    struct cart_input_axis_range under = {
        .min = 0, .max = 255, .deadzone_pct = -1,
    };
    struct cart_input_axis_range good = stick_dz15();

    CHECK_EQ(cart_input_axis_normalize(42, NULL), CART_INPUT_AXIS_INVALID);
    CHECK_EQ(cart_input_axis_normalize(42, &bad_min_max),
             CART_INPUT_AXIS_INVALID);
    CHECK_EQ(cart_input_axis_normalize(42, &flat), CART_INPUT_AXIS_INVALID);
    CHECK_EQ(cart_input_axis_normalize(42, &over), CART_INPUT_AXIS_INVALID);
    CHECK_EQ(cart_input_axis_normalize(42, &under), CART_INPUT_AXIS_INVALID);

    /* INT32 extremes inside an otherwise valid range clamp cleanly. */
    CHECK_EQ(cart_input_axis_normalize(INT32_MIN, &good), -100);
    CHECK_EQ(cart_input_axis_normalize(INT32_MAX, &good), 100);
}

/* Symmetry ON A SYMMETRIC SHAPE (even span): mirror inputs give mirrored
 * outputs exactly. The odd live shapes are intentionally exempt — their
 * physical throws differ by one unit and this normalizer is faithful to
 * that rather than cosmetically forcing symmetry (documented in the
 * design doc). */
static void test_symmetry_on_symmetric_shape(void)
{
    struct cart_input_axis_range range = {
        .min = 0, .max = 256, .deadzone_pct = 15,
    };

    for (int32_t k = 0; k <= 147; k++) {
        long low = cart_input_axis_normalize(k, &range);
        long high = cart_input_axis_normalize(256 - k, &range);

        if (low != -high) {
            fprintf(stderr, "FAIL symmetry: k=%ld low=%ld high=%ld\n",
                    (long)k, low, high);
            exit(1);
        }
    }
}

int main(void)
{
    test_stick_shape_deadzone_15();
    test_trigger_shape_identical_math();
    test_hat_shape_exactness();
    test_even_span_variant();
    test_zero_deadzone_center_only();
    test_half_deadzone_wide_plateau();
    test_invalid_inputs_rejected();
    test_symmetry_on_symmetric_shape();
    printf("all input axis normalization tests passed\n");
    return 0;
}
