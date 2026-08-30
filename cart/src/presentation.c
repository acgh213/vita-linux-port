#include <cart/presentation.h>

#include <string.h>

void cart_presentation_upscale(uint32_t *framebuffer,
                               const uint32_t *logical,
                               uint32_t *row)
{
    if (framebuffer == NULL || logical == NULL || row == NULL)
        return;

    for (size_t y = 0; y < CART_PRESENTATION_LOGICAL_HEIGHT; y++) {
        for (size_t x = 0; x < CART_PRESENTATION_LOGICAL_WIDTH; x++)
            for (size_t k = 0; k < CART_PRESENTATION_SCALE; k++)
                row[x * CART_PRESENTATION_SCALE + k] =
                    logical[y * CART_PRESENTATION_LOGICAL_WIDTH + x];
        for (size_t k = 0; k < CART_PRESENTATION_SCALE; k++)
            memcpy(framebuffer +
                       (y * CART_PRESENTATION_SCALE + k) *
                           CART_PRESENTATION_FRAMEBUFFER_WIDTH,
                   row,
                   CART_PRESENTATION_FRAMEBUFFER_WIDTH * sizeof(*row));
    }
}
