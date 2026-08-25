#include <cart/canvas.h>

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void test_clipping_and_color_order(void)
{
    uint32_t guarded[20];
    struct cart_canvas canvas;

    for (size_t index = 0; index < 20; index++)
        guarded[index] = 0xdecafbadU;
    expect(cart_canvas_init(&canvas, guarded + 4, 4, 3, 4) == 0,
           "canvas initialization");

    cart_canvas_px(&canvas, -1, 0, 0xff000001U);
    cart_canvas_px(&canvas, 4, 2, 0xff000002U);
    cart_canvas_px(&canvas, 3, 2, cart_rgba(0x11, 0x22, 0x33));
    cart_canvas_rect(&canvas, -2, 1, 4, 3, 0xff030201U);

    expect(cart_rgba(0x11, 0x22, 0x33) == 0xff332211U,
           "RGBA memory-order packing");
    expect(canvas.pixels[2 * canvas.stride + 3] == 0xff332211U,
           "in-bounds pixel write");
    expect(canvas.pixels[1 * canvas.stride + 0] == 0xff030201U,
           "clipped rectangle left edge");
    expect(canvas.pixels[2 * canvas.stride + 1] == 0xff030201U,
           "clipped rectangle bottom edge");
    for (size_t index = 0; index < 4; index++)
        expect(guarded[index] == 0xdecafbadU, "prefix guard retained");
    for (size_t index = 16; index < 20; index++)
        expect(guarded[index] == 0xdecafbadU, "suffix guard retained");
}

static void test_mix_endpoints(void)
{
    const uint32_t from = cart_rgba(10, 20, 30);
    const uint32_t to = cart_rgba(110, 120, 130);

    expect(cart_mix(from, to, 0) == from, "mix zero endpoint");
    expect(cart_mix(from, to, 256) == to, "mix full endpoint");
    expect(cart_mix(from, to, 999) == to, "mix clamps above full endpoint");
}

static void test_shape_bounds(void)
{
    uint32_t guarded[25];
    struct cart_canvas canvas;

    for (size_t index = 0; index < 25; index++)
        guarded[index] = 0;
    guarded[0] = 0xdecafbadU;
    guarded[24] = 0xdecafbadU;
    expect(cart_canvas_init(&canvas, guarded + 1, 5, 4, 5) == 0,
           "shape canvas initialization");

    cart_canvas_circle(&canvas, 0, 0, 3, 0xff7a6b5cU);
    cart_canvas_rect(&canvas, 4, 3, 5, 5, 0xff4a3b2cU);

    expect(canvas.pixels[0] == 0xff7a6b5cU, "circle includes origin");
    expect(canvas.pixels[3 * canvas.stride + 4] == 0xff4a3b2cU,
           "rectangle includes final in-bounds pixel");
    expect(guarded[0] == 0xdecafbadU, "shape prefix guard retained");
    expect(guarded[24] == 0xdecafbadU, "shape suffix guard retained");
}

static void test_ppm_and_overflow(void)
{
    uint32_t pixels[2] = { 0xff332211U, 0xff665544U };
    struct cart_canvas canvas;
    struct cart_canvas overflow;
    char bytes[32];
    FILE *stream;
    size_t count;

    expect(cart_canvas_init(&canvas, pixels, 2, 1, 2) == 0,
           "PPM canvas initialization");
    stream = tmpfile();
    expect(stream != NULL, "temporary PPM stream");
    expect(cart_canvas_write_ppm(&canvas, stream) == 0, "PPM write succeeds");
    expect(fflush(stream) == 0, "PPM stream flush");
    expect(fseek(stream, 0, SEEK_SET) == 0, "PPM stream rewind");
    count = fread(bytes, 1, sizeof(bytes), stream);
    expect(count == 17, "PPM header and RGB body length");
    expect(memcmp(bytes, "P6\n2 1\n255\n\x11\x22\x33\x44\x55\x66", 17) == 0,
           "PPM header and RGB byte order");
    expect(fclose(stream) == 0, "PPM stream close");

    expect(cart_canvas_init(&overflow, pixels, SIZE_MAX, 2, SIZE_MAX) != 0,
           "overflowing canvas rejected");
}

int main(void)
{
    test_clipping_and_color_order();
    test_mix_endpoints();
    test_shape_bounds();
    test_ppm_and_overflow();
    printf("all canvas tests passed\n");
    return 0;
}
