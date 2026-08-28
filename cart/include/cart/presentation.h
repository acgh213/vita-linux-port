#ifndef CART_PRESENTATION_H
#define CART_PRESENTATION_H

#include <stdint.h>

#define CART_PRESENTATION_LOGICAL_WIDTH 320U
#define CART_PRESENTATION_LOGICAL_HEIGHT 180U
#define CART_PRESENTATION_SCALE 4U
#define CART_PRESENTATION_FRAMEBUFFER_WIDTH \
    (CART_PRESENTATION_LOGICAL_WIDTH * CART_PRESENTATION_SCALE)
#define CART_PRESENTATION_FRAMEBUFFER_HEIGHT \
    (CART_PRESENTATION_LOGICAL_HEIGHT * CART_PRESENTATION_SCALE)

/* Expand one complete logical frame directly into the linear framebuffer.
 * `row` is caller-owned scratch space for one output row; the function never
 * needs a second full-frame presentation buffer. */
void cart_presentation_upscale(uint32_t *framebuffer,
                               const uint32_t *logical,
                               uint32_t *row);

#endif
