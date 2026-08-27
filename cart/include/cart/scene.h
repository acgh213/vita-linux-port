#ifndef CART_SCENE_H
#define CART_SCENE_H

#include <stddef.h>
#include <stdint.h>

#include <cart/canvas.h>

enum cart_scene_capability {
    CART_SCENE_CAP_2D = 1U << 0,
};

enum cart_scene_phase {
    CART_SCENE_RENDER_ROWS,
    CART_SCENE_RENDER_OVERLAY,
};

struct cart_scene_render_context {
    struct cart_canvas *canvas;
    int row_start;
    int row_end;
    uint64_t frame;
    enum cart_scene_phase phase;
};

typedef void (*cart_scene_render_fn)(const struct cart_scene_render_context *context);

struct cart_scene {
    uint32_t id;
    const char *display_name;
    size_t preferred_width;
    size_t preferred_height;
    uint32_t capabilities;
    cart_scene_render_fn render;
};

int cart_scenes_init(void);
size_t cart_scene_count(void);
const struct cart_scene *cart_scene_at(size_t index);
const struct cart_scene *cart_scene_find(uint32_t id);
const struct cart_scene *cart_scene_next(uint32_t id);
const struct cart_scene *cart_scene_previous(uint32_t id);

#endif
