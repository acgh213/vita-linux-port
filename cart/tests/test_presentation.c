#include <cart/presentation.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL %s\n", message);
        exit(1);
    }
}

int main(void)
{
    uint32_t logical[CART_PRESENTATION_LOGICAL_WIDTH * CART_PRESENTATION_LOGICAL_HEIGHT];
    uint32_t framebuffer[CART_PRESENTATION_FRAMEBUFFER_WIDTH * CART_PRESENTATION_FRAMEBUFFER_HEIGHT];
    uint32_t row[CART_PRESENTATION_FRAMEBUFFER_WIDTH];

    for (size_t y = 0; y < CART_PRESENTATION_LOGICAL_HEIGHT; y++)
        for (size_t x = 0; x < CART_PRESENTATION_LOGICAL_WIDTH; x++)
            logical[y * CART_PRESENTATION_LOGICAL_WIDTH + x] =
                0xff000000U | ((uint32_t)y << 8) | (uint32_t)x;

    cart_presentation_upscale(framebuffer, logical, row);

    for (size_t y = 0; y < CART_PRESENTATION_LOGICAL_HEIGHT; y++) {
        for (size_t x = 0; x < CART_PRESENTATION_LOGICAL_WIDTH; x++) {
            uint32_t expected = logical[y * CART_PRESENTATION_LOGICAL_WIDTH + x];
            for (size_t ky = 0; ky < CART_PRESENTATION_SCALE; ky++)
                for (size_t kx = 0; kx < CART_PRESENTATION_SCALE; kx++)
                    expect(framebuffer[(y * CART_PRESENTATION_SCALE + ky) *
                                       CART_PRESENTATION_FRAMEBUFFER_WIDTH +
                                       x * CART_PRESENTATION_SCALE + kx] == expected,
                           "nearest-neighbor frame expansion preserves each pixel");
        }
    }

    puts("presentation expansion tests passed");
    return 0;
}
