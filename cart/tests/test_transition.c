#include <cart/transition.h>

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
    struct cart_transition transition;

    cart_transition_begin(&transition, 0, 1);
    expect(cart_transition_active(&transition), "different scenes activate transition");
    expect(cart_transition_sources_need_render(&transition),
           "new transition requests source rendering");
    cart_transition_mark_sources_rendered(&transition);
    expect(!cart_transition_sources_need_render(&transition),
           "rendered transition sources are cached");

    for (unsigned int frame = 0; frame < CART_TRANSITION_FRAMES; frame++) {
        cart_transition_advance(&transition);
        expect(!cart_transition_sources_need_render(&transition),
               "cached sources remain cached while transition advances");
    }
    expect(!cart_transition_active(&transition),
           "transition completes after configured frame count");

    cart_transition_begin(&transition, 1, 2);
    expect(cart_transition_sources_need_render(&transition),
           "new transition invalidates the previous source cache");
    cart_transition_mark_sources_rendered(&transition);
    cart_transition_begin(&transition, 2, 2);
    expect(!cart_transition_active(&transition),
           "same-scene transition is inert");
    expect(!cart_transition_sources_need_render(&transition),
           "inert transition does not request rendering");

    puts("transition source-cache tests passed");
    return 0;
}
