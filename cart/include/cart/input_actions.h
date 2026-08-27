#ifndef CART_INPUT_ACTIONS_H
#define CART_INPUT_ACTIONS_H

#include <stdint.h>

/* Shared action vocabulary for the whole input stack.
 *
 * Owned here (input_actions.h) since the evdev decoder, the lifecycle
 * engine, and the runtime all consume it; cart/input.h includes this
 * header rather than redefining it. Values are ABI: do not reorder.
 */
enum cart_input_action {
    CART_INPUT_NONE = 0,
    CART_INPUT_NEXT = 1,
    CART_INPUT_PREVIOUS = 2,
    CART_INPUT_SELECT = 3,
    CART_INPUT_BACK = 4,
    CART_INPUT_MENU = 5,
    CART_INPUT_QUIT = 6,
};

/* Map an EV_KEY event code to an action. Returns CART_INPUT_NONE for every
 * code without a row in the action map (unknown buttons, d-pad up/down in
 * v1, digital triggers). Pure: no syscalls, no state. */
int cart_input_map_key(uint16_t code);

/* Repeat policy: true only for a press (value == 1). Releases (0) and key
 * repeats (2) and any larger value emit nothing. */
int cart_input_key_emits_action(uint16_t value);

#endif
