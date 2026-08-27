#include <cart/canvas.h>

#include <limits.h>

static int cart_canvas_valid(const struct cart_canvas *canvas)
{
    if (canvas == NULL || canvas->pixels == NULL || canvas->width == 0 ||
        canvas->height == 0 || canvas->stride < canvas->width)
        return 0;
    if (canvas->height > SIZE_MAX / canvas->stride)
        return 0;
    if (canvas->stride * canvas->height > SIZE_MAX / sizeof(*canvas->pixels))
        return 0;
    return 1;
}

int cart_canvas_init(struct cart_canvas *canvas, uint32_t *pixels,
                     size_t width, size_t height, size_t stride)
{
    struct cart_canvas candidate;

    if (canvas == NULL)
        return -1;
    candidate = (struct cart_canvas) {
        .pixels = pixels,
        .width = width,
        .height = height,
        .stride = stride,
    };
    if (!cart_canvas_valid(&candidate))
        return -1;
    *canvas = candidate;
    return 0;
}

size_t cart_canvas_byte_size(const struct cart_canvas *canvas)
{
    if (!cart_canvas_valid(canvas))
        return 0;
    return canvas->stride * canvas->height * sizeof(*canvas->pixels);
}

static int clamp_channel(int value)
{
    if (value < 0)
        return 0;
    if (value > 255)
        return 255;
    return value;
}

uint32_t cart_rgba(int red, int green, int blue)
{
    return (uint32_t)clamp_channel(red) |
           ((uint32_t)clamp_channel(green) << 8) |
           ((uint32_t)clamp_channel(blue) << 16) | 0xff000000U;
}

uint32_t cart_mix(uint32_t from, uint32_t to, unsigned int amount)
{
    int from_red;
    int from_green;
    int from_blue;
    int to_red;
    int to_green;
    int to_blue;

    if (amount > 256U)
        amount = 256U;
    from_red = (int)(from & 0xffU);
    from_green = (int)((from >> 8) & 0xffU);
    from_blue = (int)((from >> 16) & 0xffU);
    to_red = (int)(to & 0xffU);
    to_green = (int)((to >> 8) & 0xffU);
    to_blue = (int)((to >> 16) & 0xffU);
    return cart_rgba(from_red + ((to_red - from_red) * (int)amount >> 8),
                     from_green + ((to_green - from_green) * (int)amount >> 8),
                     from_blue + ((to_blue - from_blue) * (int)amount >> 8));
}

void cart_canvas_px(struct cart_canvas *canvas, int x, int y, uint32_t color)
{
    if (!cart_canvas_valid(canvas) || x < 0 || y < 0 ||
        (size_t)x >= canvas->width || (size_t)y >= canvas->height)
        return;
    canvas->pixels[(size_t)y * canvas->stride + (size_t)x] = color;
}

void cart_canvas_rect(struct cart_canvas *canvas, int x, int y,
                      int width, int height, uint32_t color)
{
    int64_t x0;
    int64_t y0;
    int64_t x1;
    int64_t y1;

    if (!cart_canvas_valid(canvas) || width <= 0 || height <= 0)
        return;
    x0 = x;
    y0 = y;
    x1 = x0 + width;
    y1 = y0 + height;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > (int64_t)canvas->width)
        x1 = (int64_t)canvas->width;
    if (y1 > (int64_t)canvas->height)
        y1 = (int64_t)canvas->height;
    if (x0 >= x1 || y0 >= y1)
        return;
    for (int64_t row = y0; row < y1; row++)
        for (int64_t column = x0; column < x1; column++)
            canvas->pixels[(size_t)row * canvas->stride + (size_t)column] = color;
}

void cart_canvas_circle(struct cart_canvas *canvas, int center_x, int center_y,
                        int radius, uint32_t color)
{
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;
    int64_t squared_radius;

    if (!cart_canvas_valid(canvas) || radius < 0)
        return;
    left = (int64_t)center_x - radius;
    top = (int64_t)center_y - radius;
    right = (int64_t)center_x + radius;
    bottom = (int64_t)center_y + radius;
    if (left < 0)
        left = 0;
    if (top < 0)
        top = 0;
    if (right >= (int64_t)canvas->width)
        right = (int64_t)canvas->width - 1;
    if (bottom >= (int64_t)canvas->height)
        bottom = (int64_t)canvas->height - 1;
    if (left > right || top > bottom)
        return;
    squared_radius = (int64_t)radius * radius;
    for (int64_t row = top; row <= bottom; row++) {
        const int64_t delta_y = row - center_y;
        for (int64_t column = left; column <= right; column++) {
            const int64_t delta_x = column - center_x;
            if (delta_x * delta_x + delta_y * delta_y <= squared_radius)
                canvas->pixels[(size_t)row * canvas->stride + (size_t)column] = color;
        }
    }
}

int cart_canvas_write_ppm(const struct cart_canvas *canvas, FILE *stream)
{
    if (!cart_canvas_valid(canvas) || stream == NULL)
        return -1;
    if (fprintf(stream, "P6\n%zu %zu\n255\n", canvas->width, canvas->height) < 0)
        return -1;
    for (size_t row = 0; row < canvas->height; row++) {
        for (size_t column = 0; column < canvas->width; column++) {
            const uint32_t color = canvas->pixels[row * canvas->stride + column];
            const uint8_t rgb[3] = {
                (uint8_t)(color & 0xffU),
                (uint8_t)((color >> 8) & 0xffU),
                (uint8_t)((color >> 16) & 0xffU),
            };
            if (fwrite(rgb, 1, sizeof(rgb), stream) != sizeof(rgb))
                return -1;
        }
    }
    return ferror(stream) ? -1 : 0;
}
