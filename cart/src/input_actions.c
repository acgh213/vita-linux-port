/* Pure evdev key-code -> action decoder for the B5 demo cart.
 *
 * Table source of truth:
 *   - Syscon d-pad: vita-toolkit-foundation
 *     linux_vita/drivers/input/joystick/vita-buttons.c lines 56-59 report
 *     BTN_DPAD_UP/DOWN/LEFT/RIGHT (544/545/546/547). UP/DOWN stay
 *     unmapped in v1 (design reserves vertical gestures); LEFT -> PREVIOUS,
 *     RIGHT -> NEXT.
 *   - Face/shoulder/system buttons + keyboard QUIT paths: B5 design doc
 *     (.hermes/plans/2026-08-26-b5-input-design.md, "Action map", union of
 *     the captured Syscon + DS4 device contracts).
 *
 * No syscalls, no state: safe to unit-test and to reuse from any poll loop.
 */
#include <cart/input_actions.h>

#include <stddef.h>

struct key_action_row {
    uint16_t code;
    int8_t action;
};

/* Ordered by code for a linear scan; sentinel-terminated. */
static const struct key_action_row KEY_ACTION_TABLE[] = {
    { 1,   CART_INPUT_QUIT },     /* KEY_ESC */
    { 16,  CART_INPUT_QUIT },     /* KEY_Q */
    { 304, CART_INPUT_SELECT },   /* BTN_SOUTH (Cross) */
    { 305, CART_INPUT_BACK },     /* BTN_EAST (Circle) */
    { 307, CART_INPUT_NEXT },     /* BTN_NORTH (Triangle) */
    { 308, CART_INPUT_PREVIOUS }, /* BTN_WEST (Square, DS4) */
    { 310, CART_INPUT_PREVIOUS }, /* BTN_TL / L1 */
    { 311, CART_INPUT_NEXT },     /* BTN_TR / R1 */
    { 314, CART_INPUT_MENU },     /* BTN_SELECT / Share */
    { 315, CART_INPUT_MENU },     /* BTN_START / Options */
    { 316, CART_INPUT_QUIT },     /* BTN_MODE / PS */
    { 546, CART_INPUT_PREVIOUS }, /* BTN_DPAD_LEFT (Syscon) */
    { 547, CART_INPUT_NEXT },     /* BTN_DPAD_RIGHT (Syscon) */
};

#define TABLE_LEN (sizeof(KEY_ACTION_TABLE) / sizeof(KEY_ACTION_TABLE[0]))

int cart_input_map_key(uint16_t code)
{
    for (size_t i = 0; i < TABLE_LEN; i++) {
        if (KEY_ACTION_TABLE[i].code == code)
            return KEY_ACTION_TABLE[i].action;
    }
    return CART_INPUT_NONE;
}

int cart_input_key_emits_action(uint16_t value)
{
    return value == 1;
}
