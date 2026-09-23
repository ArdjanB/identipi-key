/*
 * buttons.c -- joystick handling
 */
#include "buttons.h"
#include "version.h"
#include "pico/stdlib.h"

#define DEBOUNCE_MS 25

static const uint8_t pins[] = {
    0,                  /* BTN_NONE placeholder */
    PIN_JOY_UP,
    PIN_JOY_DOWN,
    PIN_JOY_LEFT,
    PIN_JOY_RIGHT,
    PIN_JOY_SEL,
};
#define BTN_COUNT (sizeof(pins) / sizeof(pins[0]))

static bool            last_state[BTN_COUNT];   /* true = pressed */
static absolute_time_t last_change[BTN_COUNT];

void buttons_init(void) {
    for (size_t i = 1; i < BTN_COUNT; i++) {
        gpio_init(pins[i]);
        gpio_set_dir(pins[i], GPIO_IN);
        gpio_pull_up(pins[i]);
        last_state[i] = false;
        last_change[i] = get_absolute_time();
    }
    sleep_ms(10);
}

bool buttons_held(button_t b) {
    if (b == BTN_NONE || (size_t)b >= BTN_COUNT) return false;
    return gpio_get(pins[b]) == 0;      /* active low */
}

button_t buttons_get(void) {
    absolute_time_t now = get_absolute_time();

    for (size_t i = 1; i < BTN_COUNT; i++) {
        bool pressed = gpio_get(pins[i]) == 0;

        if (pressed != last_state[i]) {
            int64_t since = absolute_time_diff_us(last_change[i], now) / 1000;
            if (since < DEBOUNCE_MS) continue;

            last_change[i] = now;
            last_state[i] = pressed;
            if (pressed) return (button_t)i;    /* edge: press only */
        }
    }
    return BTN_NONE;
}

button_t buttons_wait(uint32_t mask, uint32_t timeout_ms) {
    absolute_time_t deadline = timeout_ms
        ? make_timeout_time_ms(timeout_ms)
        : at_the_end_of_time;

    while (!time_reached(deadline)) {
        button_t b = buttons_get();
        if (b != BTN_NONE && (mask & BTN_MASK(b))) return b;
        sleep_ms(5);
    }
    return BTN_NONE;
}

#define REPEAT_INITIAL_MS 350
#define REPEAT_INTERVAL_MS 100

static bool is_directional(button_t b) {
    return b == BTN_UP || b == BTN_DOWN || b == BTN_LEFT || b == BTN_RIGHT;
}

button_t buttons_wait_repeat(uint32_t mask, uint32_t timeout_ms) {
    static button_t        held_btn = BTN_NONE;
    static absolute_time_t next_repeat;

    absolute_time_t deadline = timeout_ms
        ? make_timeout_time_ms(timeout_ms)
        : at_the_end_of_time;

    while (!time_reached(deadline)) {
        button_t b = buttons_get();
        if (b != BTN_NONE && (mask & BTN_MASK(b))) {
            held_btn = is_directional(b) ? b : BTN_NONE;
            next_repeat = make_timeout_time_ms(REPEAT_INITIAL_MS);
            return b;
        }

        if (held_btn != BTN_NONE) {
            if (!buttons_held(held_btn)) {
                held_btn = BTN_NONE;
            } else if (time_reached(next_repeat)) {
                next_repeat = make_timeout_time_ms(REPEAT_INTERVAL_MS);
                return held_btn;
            }
        }
        sleep_ms(5);
    }
    held_btn = BTN_NONE;
    return BTN_NONE;
}
