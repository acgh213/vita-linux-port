#ifndef CART_CANVAS_H
#define CART_CANVAS_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

struct cart_canvas {
    uint32_t *pixels;
    size_t width;
    size_t height;
    size_t stride;
};

int cart_canvas_init(struct cart_canvas *canvas, uint32_t *pixels,
                     size_t width, size_t height, size_t stride);
size_t cart_canvas_byte_size(const struct cart_canvas *canvas);

uint32_t cart_rgba(int red, int green, int blue);
uint32_t cart_mix(uint32_t from, uint32_t to, unsigned int amount);

void cart_canvas_px(struct cart_canvas *canvas, int x, int y, uint32_t color);
void cart_canvas_rect(struct cart_canvas *canvas, int x, int y,
                      int width, int height, uint32_t color);
void cart_canvas_circle(struct cart_canvas *canvas, int center_x, int center_y,
                        int radius, uint32_t color);
int cart_canvas_write_ppm(const struct cart_canvas *canvas, FILE *stream);

#endif
