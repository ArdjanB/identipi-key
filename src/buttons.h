/*
 * buttons.h -- 5-way joystick, active low with pull-ups
 *
 * GP14/GP15 are swapped relative to SB Components' published pinout.
 * Verified on hardware: physical UP is GP15, physical RIGHT is GP14.
 */
#ifndef BUTTONS_H
#define BUTTONS_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BTN_NONE = 0,
    BTN_UP,
    BTN_DOWN,
    BTN_LEFT,
    BTN_RIGHT,
    BTN_SEL,
} button_t;

void     buttons_init(void);

/* Returns a button once per physical press (edge triggered, debounced). */
button_t buttons_get(void);

/* Current held state, for things like boot-time key holds. */
bool     buttons_held(button_t b);

/* Blocks until one of the buttons in the mask is pressed, or timeout.
 * Pass 0 for no timeout. Mask is a bitfield of (1 << button_t). */
button_t buttons_wait(uint32_t mask, uint32_t timeout_ms);

/* Like buttons_wait(), but a held UP/DOWN/LEFT/RIGHT auto-repeats after an
 * initial delay -- for scrolling through lists/grids without a press per
 * step. SEL never repeats: spamming whatever action is under the cursor
 * just because it was held would be surprising (e.g. re-inserting the
 * same character over and over on the on-screen keyboard). */
button_t buttons_wait_repeat(uint32_t mask, uint32_t timeout_ms);

#define BTN_MASK(b) (1u << (b))
#define BTN_MASK_ANY (BTN_MASK(BTN_UP) | BTN_MASK(BTN_DOWN) | \
                      BTN_MASK(BTN_LEFT) | BTN_MASK(BTN_RIGHT) | \
                      BTN_MASK(BTN_SEL))

#endif /* BUTTONS_H */
