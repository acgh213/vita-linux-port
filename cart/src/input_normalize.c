#include <cart/input_normalize.h>

#include <stddef.h>

/* Integer axis normalization with center deadzone.
 *
 * Bands relative to hinge center c = min + span/2 and half-deadzone
 * n = span * deadzone_pct / 200 (both integer floor):
 *   [c-n .. c+n]   -> 0
 *   [min .. c-n-1] -> ((raw - lo) * 100) / (lo - min)      [-100..-1]
 *   [c+n+1 .. max] -> ((raw - hi) * 100) / (max - hi)      [1..100]
 * where lo = c-n and hi = c+n. raw is clamped to [min,max] before any
 * arithmetic so INT32 extremes cannot overflow intermediates; products
 * are widened to int64 for the same reason.
 *
 * Note: on wide-band shapes the first raw step beyond the rim can
 * truncate back to 0 (documented + pinned in the test suite); full throw
 * always reaches exactly +-100. */
int cart_input_axis_normalize(int32_t raw,
                              const struct cart_input_axis_range *range)
{
    int32_t span;
    int64_t half;
    int64_t low;
    int64_t high;
    int64_t clamped;

    if (range == NULL || range->max <= range->min ||
        range->deadzone_pct < 0 || range->deadzone_pct > 50)
        return CART_INPUT_AXIS_INVALID;

    clamped = raw;
    if (clamped < range->min)
        clamped = range->min;
    if (clamped > range->max)
        clamped = range->max;

    span = range->max - range->min;
    half = (int64_t)span * range->deadzone_pct / 200;
    low = (int64_t)range->min + span / 2 - half;
    high = low + 2 * half;

    if (clamped >= low && clamped <= high)
        return 0;

    if (clamped < low)
        return (int)((clamped - low) * 100 /
                     (low - (int64_t)range->min));

    return (int)((clamped - high) * 100 /
                 ((int64_t)range->max - high));
}
