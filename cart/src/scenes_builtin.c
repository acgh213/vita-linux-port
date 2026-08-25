#include <cart/scene.h>

int cart_legacy_scenes_init(void);
void cart_legacy_scene_candy(const struct cart_scene_render_context *context);
void cart_legacy_scene_girl_mode(const struct cart_scene_render_context *context);
void cart_legacy_scene_bubblegum(const struct cart_scene_render_context *context);
void cart_legacy_scene_butterfly_riot(const struct cart_scene_render_context *context);
void cart_legacy_scene_hyperdrive(const struct cart_scene_render_context *context);
void cart_legacy_scene_too_much(const struct cart_scene_render_context *context);

static const struct cart_scene builtin_scenes[] = {
    {
        .id = 0,
        .display_name = "Candy Vortex",
        .preferred_width = 320,
        .preferred_height = 180,
        .capabilities = CART_SCENE_CAP_2D,
        .render = cart_legacy_scene_candy,
    },
    {
        .id = 1,
        .display_name = "Girl Mode",
        .preferred_width = 320,
        .preferred_height = 180,
        .capabilities = CART_SCENE_CAP_2D,
        .render = cart_legacy_scene_girl_mode,
    },
    {
        .id = 2,
        .display_name = "Bubblegum Overdrive",
        .preferred_width = 320,
        .preferred_height = 180,
        .capabilities = CART_SCENE_CAP_2D,
        .render = cart_legacy_scene_bubblegum,
    },
    {
        .id = 3,
        .display_name = "Butterfly Riot",
        .preferred_width = 320,
        .preferred_height = 180,
        .capabilities = CART_SCENE_CAP_2D,
        .render = cart_legacy_scene_butterfly_riot,
    },
    {
        .id = 4,
        .display_name = "She Her Hyperdrive",
        .preferred_width = 320,
        .preferred_height = 180,
        .capabilities = CART_SCENE_CAP_2D,
        .render = cart_legacy_scene_hyperdrive,
    },
    {
        .id = 5,
        .display_name = "Too Much Is Enough",
        .preferred_width = 320,
        .preferred_height = 180,
        .capabilities = CART_SCENE_CAP_2D,
        .render = cart_legacy_scene_too_much,
    },
};

int cart_scenes_init(void)
{
    return cart_legacy_scenes_init();
}

size_t cart_scene_count(void)
{
    return sizeof(builtin_scenes) / sizeof(builtin_scenes[0]);
}

const struct cart_scene *cart_scene_at(size_t index)
{
    if (index >= cart_scene_count())
        return NULL;
    return &builtin_scenes[index];
}

const struct cart_scene *cart_scene_find(uint32_t id)
{
    for (size_t index = 0; index < cart_scene_count(); index++) {
        if (builtin_scenes[index].id == id)
            return &builtin_scenes[index];
    }
    return NULL;
}

const struct cart_scene *cart_scene_next(uint32_t id)
{
    for (size_t index = 0; index < cart_scene_count(); index++) {
        if (builtin_scenes[index].id == id)
            return &builtin_scenes[(index + 1) % cart_scene_count()];
    }
    return NULL;
}

const struct cart_scene *cart_scene_previous(uint32_t id)
{
    for (size_t index = 0; index < cart_scene_count(); index++) {
        if (builtin_scenes[index].id == id)
            return &builtin_scenes[(index + cart_scene_count() - 1) % cart_scene_count()];
    }
    return NULL;
}
