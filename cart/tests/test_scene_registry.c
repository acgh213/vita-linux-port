#include <cart/scene.h>

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

static void test_stable_builtin_order(void)
{
    static const uint32_t expected_ids[] = { 0, 1, 2, 3, 4, 5 };
    static const char *const expected_names[] = {
        "Candy Vortex", "Girl Mode", "Bubblegum Overdrive",
        "Butterfly Riot", "She Her Hyperdrive", "Too Much Is Enough",
    };

    expect(cart_scene_count() == 6, "six built-in scenes");
    for (size_t index = 0; index < cart_scene_count(); index++) {
        const struct cart_scene *scene = cart_scene_at(index);
        expect(scene != NULL, "scene exists at stable index");
        expect(scene->id == expected_ids[index], "stable scene ID order");
        expect(strcmp(scene->display_name, expected_names[index]) == 0,
               "stable scene display name");
        expect(scene->preferred_width == 320 && scene->preferred_height == 180,
               "legacy logical dimensions");
        expect(scene->capabilities == CART_SCENE_CAP_2D,
               "legacy 2D capability");
        expect(scene->render != NULL, "scene render callback");
    }
    expect(cart_scene_at(cart_scene_count()) == NULL, "out-of-range index rejected");
}

static void test_unique_lookup_and_navigation(void)
{
    for (size_t index = 0; index < cart_scene_count(); index++) {
        const struct cart_scene *scene = cart_scene_at(index);
        const struct cart_scene *next;
        const struct cart_scene *previous;

        for (size_t other = index + 1; other < cart_scene_count(); other++)
            expect(scene->id != cart_scene_at(other)->id, "scene IDs are unique");
        expect(cart_scene_find(scene->id) == scene, "scene lookup preserves entry identity");
        next = cart_scene_next(scene->id);
        previous = cart_scene_previous(scene->id);
        expect(next == cart_scene_at((index + 1) % cart_scene_count()),
               "next scene wraps in stable order");
        expect(previous == cart_scene_at((index + cart_scene_count() - 1) % cart_scene_count()),
               "previous scene wraps in stable order");
    }
    expect(cart_scene_find(99) == NULL, "missing scene lookup rejected");
    expect(cart_scene_next(99) == NULL, "next missing scene rejected");
    expect(cart_scene_previous(99) == NULL, "previous missing scene rejected");
}

int main(void)
{
    expect(cart_scenes_init() == 0, "scene system initialization");
    test_stable_builtin_order();
    test_unique_lookup_and_navigation();
    printf("all scene registry tests passed\n");
    return 0;
}
