#ifndef CART_INPUT_ACTIONS_H
#define CART_INPUT_ACTIONS_H

#include <stdint.h>

/* Parallel action constants for the pure evdev key->action decoder.
 *
 * These mirror the six-action ordering that cart/input.h owns
 * (CART_INPUT_NONE=0, NEXT, PREVIOUS, SELECT, BACK, MENU, QUIT) so the
 * values stay binary-compatible when the decoder is wired into the full
 * input stack. This header is deliberately standalone: it must never
 * include or redefine cart/input.h.
 */
enum {
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
