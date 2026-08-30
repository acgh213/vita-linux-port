#define _GNU_SOURCE
#include <cart/input_actions.h>

#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

/* Action-map rows, union of Syscon + DS4 per the B5 design doc
 * (.hermes/plans/2026-08-26-b5-input-design.md, "Action map"):
 *   304 BTN_SOUTH  -> SELECT      305 BTN_EAST   -> BACK
 *   307 BTN_NORTH  -> NEXT        308 BTN_WEST   -> PREVIOUS
 *   310 BTN_TL/L1  -> PREVIOUS    311 BTN_TR/R1  -> NEXT
 *   314 BTN_SELECT -> MENU        315 BTN_START  -> MENU
 *   316 BTN_MODE   -> QUIT (design D5: PS button is the shutdown path)
 *   1  KEY_ESC     -> QUIT        16 KEY_Q       -> QUIT
 * D-pad (source of truth: vita-toolkit-foundation
 * linux_vita/drivers/input/joystick/vita-buttons.c lines 56-59 report
 * BTN_DPAD_UP/DOWN/LEFT/RIGHT; the design doc's "TRIGGER_HAPPY" label for
 * 544-547 is a capture-side misnomer — kernel HAPPY codes are 0x2c0+):
 *   546 BTN_DPAD_LEFT  -> PREVIOUS
 *   547 BTN_DPAD_RIGHT -> NEXT
 *   544 BTN_DPAD_UP / 545 BTN_DPAD_DOWN -> NONE (vertical reserved, v1)
 * Everything else — including 306 BTN_C, 312/313 digital triggers (absent
 * by design) — maps to CART_INPUT_NONE.
 */
struct row {
    uint16_t code;
    int action;
};

static const struct row MAPPED_ROWS[] = {
    { 1,   CART_INPUT_QUIT },      /* KEY_ESC */
    { 16,  CART_INPUT_QUIT },      /* KEY_Q */
    { 304, CART_INPUT_SELECT },    /* BTN_SOUTH */
    { 305, CART_INPUT_BACK },      /* BTN_EAST */
    { 307, CART_INPUT_NEXT },      /* BTN_NORTH */
    { 308, CART_INPUT_PREVIOUS },  /* BTN_WEST (DS4 square) */
    { 310, CART_INPUT_PREVIOUS },  /* BTN_TL / L1 */
    { 311, CART_INPUT_NEXT },      /* BTN_TR / R1 */
    { 314, CART_INPUT_MENU },      /* BTN_SELECT / Share */
    { 315, CART_INPUT_MENU },      /* BTN_START / Options */
    { 316, CART_INPUT_QUIT },      /* BTN_MODE / PS */
    { 546, CART_INPUT_PREVIOUS },  /* BTN_DPAD_LEFT */
    { 547, CART_INPUT_NEXT },      /* BTN_DPAD_RIGHT */
};
#define MAPPED_ROW_COUNT (sizeof(MAPPED_ROWS) / sizeof(MAPPED_ROWS[0]))

static int code_is_mapped(uint16_t code, int *action)
{
    for (size_t i = 0; i < MAPPED_ROW_COUNT; i++) {
        if (MAPPED_ROWS[i].code == code) {
            *action = MAPPED_ROWS[i].action;
            return 1;
        }
    }
    return 0;
}

/* Kernel uapi values the numeric contract is taken from; guards the table
 * against header/literal drift (input-event-codes.h lines 76, 91, 381-397,
 * 598-601). */
static void test_symbolic_code_values(void)
{
    CHECK(KEY_ESC == 1);
    CHECK(KEY_Q == 16);
    CHECK(BTN_SOUTH == 304);
    CHECK(BTN_EAST == 305);
    CHECK(BTN_C == 306);
    CHECK(BTN_NORTH == 307);
    CHECK(BTN_WEST == 308);
    CHECK(BTN_TL == 310);
    CHECK(BTN_TR == 311);
    CHECK(BTN_TL2 == 312);
    CHECK(BTN_SELECT == 314);
    CHECK(BTN_START == 315);
    CHECK(BTN_MODE == 316);
    CHECK(BTN_DPAD_UP == 544);
    CHECK(BTN_DPAD_DOWN == 545);
    CHECK(BTN_DPAD_LEFT == 546);
    CHECK(BTN_DPAD_RIGHT == 547);
}

/* Binary-compatible six-action ordering (design "Public API" enum). */
static void test_action_value_ordering(void)
{
    CHECK(CART_INPUT_NONE == 0);
    CHECK(CART_INPUT_NEXT == 1);
    CHECK(CART_INPUT_PREVIOUS == 2);
    CHECK(CART_INPUT_SELECT == 3);
    CHECK(CART_INPUT_BACK == 4);
    CHECK(CART_INPUT_MENU == 5);
    CHECK(CART_INPUT_QUIT == 6);
}

/* Direction 1: every table row, code -> action. */
static void test_forward_every_row(void)
{
    for (size_t i = 0; i < MAPPED_ROW_COUNT; i++) {
        int got = cart_input_map_key(MAPPED_ROWS[i].code);

        if (got != MAPPED_ROWS[i].action) {
            fprintf(stderr, "FAIL forward row %zu: code %u -> %d, want %d\n",
                    i, (unsigned)MAPPED_ROWS[i].code, got,
                    MAPPED_ROWS[i].action);
            exit(1);
        }
    }
}

/* Direction 2: exhaustive reverse scan over the whole uint16_t code space.
 * The preimage of each action must be exactly its table rows, and every
 * other code must decode to NONE — nothing unmapped may sneak in. */
static void test_reverse_exact_preimages(void)
{
    size_t hits[MAPPED_ROW_COUNT];
    memset(hits, 0, sizeof(hits));

    for (uint32_t code = 0; code <= 0xFFFFu; code++) {
        int got = cart_input_map_key((uint16_t)code);
        int want;
        int row;

        if (code_is_mapped((uint16_t)code, &want)) {
            if (got != want) {
                fprintf(stderr, "FAIL reverse: code %u -> %d, want %d\n",
                        code, got, want);
                exit(1);
            }
            for (row = 0; row < (int)MAPPED_ROW_COUNT; row++) {
                if (MAPPED_ROWS[row].code == code)
                    hits[row]++;
            }
        } else if (got != CART_INPUT_NONE) {
            fprintf(stderr,
                    "FAIL reverse: unmapped code %u -> %d, want NONE\n",
                    code, got);
            exit(1);
        }
    }
    for (size_t i = 0; i < MAPPED_ROW_COUNT; i++) {
        if (hits[i] != 1) {
            fprintf(stderr,
                    "FAIL reverse: row %zu (code %u) matched %zu times\n",
                    i, (unsigned)MAPPED_ROWS[i].code, hits[i]);
            exit(1);
        }
    }
}

static void test_explicit_unmapped_codes(void)
{
    const uint16_t unmapped[] = {
        0, 306, 312, 313, 999, 544, 545, 0x2C0, 0xFFFF,
    };

    for (size_t i = 0; i < sizeof(unmapped) / sizeof(unmapped[0]); i++) {
        int got = cart_input_map_key(unmapped[i]);

        if (got != CART_INPUT_NONE) {
            fprintf(stderr, "FAIL unmapped code %u -> %d, want NONE\n",
                    (unsigned)unmapped[i], got);
            exit(1);
        }
    }
}

/* D-pad orientation per vita-buttons.c: LEFT(546) -> PREVIOUS,
 * RIGHT(547) -> NEXT, UP(544)/DOWN(545) reserved -> NONE. */
static void test_dpad_orientation(void)
{
    CHECK(cart_input_map_key(544) == CART_INPUT_NONE);
    CHECK(cart_input_map_key(545) == CART_INPUT_NONE);
    CHECK(cart_input_map_key(546) == CART_INPUT_PREVIOUS);
    CHECK(cart_input_map_key(547) == CART_INPUT_NEXT);
    CHECK(cart_input_map_key((uint16_t)BTN_DPAD_LEFT) == CART_INPUT_PREVIOUS);
    CHECK(cart_input_map_key((uint16_t)BTN_DPAD_RIGHT) == CART_INPUT_NEXT);
    CHECK(cart_input_map_key((uint16_t)BTN_DPAD_UP) == CART_INPUT_NONE);
    CHECK(cart_input_map_key((uint16_t)BTN_DPAD_DOWN) == CART_INPUT_NONE);
}

static void test_quit_paths(void)
{
    CHECK(cart_input_map_key(1) == CART_INPUT_QUIT);
    CHECK(cart_input_map_key(16) == CART_INPUT_QUIT);
    CHECK(cart_input_map_key(316) == CART_INPUT_QUIT);
    CHECK(cart_input_map_key((uint16_t)KEY_ESC) == CART_INPUT_QUIT);
    CHECK(cart_input_map_key((uint16_t)KEY_Q) == CART_INPUT_QUIT);
    CHECK(cart_input_map_key((uint16_t)BTN_MODE) == CART_INPUT_QUIT);
}

/* Repeat policy: only value==1 (press) emits. Release (0), key repeat (2),
 * and any larger value are dropped. */
static void test_repeat_policy(void)
{
    CHECK(cart_input_key_emits_action(0) == 0);
    CHECK(cart_input_key_emits_action(1) == 1);
    CHECK(cart_input_key_emits_action(2) == 0);
    CHECK(cart_input_key_emits_action(3) == 0);
    CHECK(cart_input_key_emits_action(0xFFFF) == 0);
}

int main(void)
{
    test_symbolic_code_values();
    test_action_value_ordering();
    test_forward_every_row();
    test_reverse_exact_preimages();
    test_explicit_unmapped_codes();
    test_dpad_orientation();
    test_quit_paths();
    test_repeat_policy();
    printf("all input action mapping tests passed\n");
    return 0;
}
