#ifndef CART_INPUT_NORMALIZE_H
#define CART_INPUT_NORMALIZE_H

#include <stdint.h>

/* Sentinel returned for invalid inputs: NULL range, max <= min, or a
 * deadzone percentage outside [0, 50]. */
#define CART_INPUT_AXIS_INVALID INT32_MIN

struct cart_input_axis_range {
    int32_t min;
    int32_t max;
    int32_t deadzone_pct;
};

/* Normalize raw against range; returns percent in [-100..100] (0 inside the
 * deadzone) or CART_INPUT_AXIS_INVALID on invalid inputs. */
int cart_input_axis_normalize(int32_t raw,
                              const struct cart_input_axis_range *range);

#endif
