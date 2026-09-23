/*
 * main.c -- IdentiPi fingerprint key, milestone 3
 *
 * Milestone 1: toolchain, display, fingerprint driver, joystick, idle
 * screen, lockout (RAM-only at the time).
 * Milestone 2: USB HID keyboard output (TinyUSB), with jitter.
 * Milestone 3: flash-backed config, the real menu, enrollment, and
 * finger->password binding. The failed-attempt counter now lives in
 * flash too, per DESIGN.md 5.3. Confirmed on hardware: enroll a finger,
 * scan it, get a match. See CLAUDE.md hardware fact #7 for the enroll
 * command's privilege-byte requirement that this depends on.
 * Milestone 4 (on-screen keyboard) got pulled in early, since M3's
 * password/description entry had no other input path -- see keyboard.c.
 *
 * NOT yet implemented:
 *   M5  encrypted flash, secure boot, OTP lock
 */
#include "version.h"
#include "display.h"
#include "buttons.h"
#include "fp.h"
#include "hid.h"
#include "config.h"
#include "menu.h"

#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

#define IDLE_MOVE_MS     10000    /* reposition idle text (image retention) */
#define BACKLIGHT_MS     60000    /* blank backlight after this idle time */
#define RESULT_MS         2000    /* minimum time a scan result stays up */
#define GATE_TIMEOUT_MS  15000    /* time allowed to scan for menu access */

/* Escalating lockout after repeated failures. The sensor has no
 * brute-force protection of its own. The counter lives in flash
 * (config_t::failed_attempts) so it survives a replug; the countdown
 * itself is RAM-only and recomputed fresh at boot from that counter --
 * see config.h for why an absolute "until" time can't be persisted
 * meaningfully on hardware with no battery-backed clock. */
#define FAIL_THRESHOLD 5
static const uint32_t lockout_steps_ms[] = {5000, 15000, 60000, 300000, 900000};
#define LOCKOUT_STEPS (sizeof(lockout_steps_ms) / sizeof(lockout_steps_ms[0]))

static absolute_time_t lockout_until;
static bool            locked_out = false;

static void arm_lockout_from_count(uint32_t failed_attempts) {
    if (failed_attempts < FAIL_THRESHOLD) {
        locked_out = false;
        return;
    }
    uint32_t step = failed_attempts - FAIL_THRESHOLD;
    if (step >= LOCKOUT_STEPS) step = LOCKOUT_STEPS - 1;
    lockout_until = make_timeout_time_ms(lockout_steps_ms[step]);
    locked_out = true;
}

static void register_failure(config_t *cfg) {
    cfg->failed_attempts++;
    config_save(cfg);
    arm_lockout_from_count(cfg->failed_attempts);
    printf("failed attempt #%lu\n", (unsigned long) cfg->failed_attempts);
}

static void register_success(config_t *cfg) {
    cfg->failed_attempts = 0;
    config_save(cfg);
    locked_out = false;
}

static bool lockout_active(uint32_t *seconds_left) {
    if (!locked_out) return false;
    if (time_reached(lockout_until)) {
        locked_out = false;
        return false;
    }
    if (seconds_left) {
        int64_t us = absolute_time_diff_us(get_absolute_time(), lockout_until);
        *seconds_left = (uint32_t) ((us / 1000000) + 1);
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Screens                                                             */
/* ------------------------------------------------------------------ */

static void screen_splash(void) {
    display_clear(COL_BLACK);
    display_text_centred(24, FIRMWARE_NAME, COL_CYAN, TEXT_NO_BG, 2);
    display_text_centred(64, "v" FIRMWARE_VERSION, COL_GREY, TEXT_NO_BG, 1);
    display_text_centred(78, "built " FIRMWARE_BUILD_TIME, COL_DIM, TEXT_NO_BG, 1);
    display_text_centred(96, "starting...", COL_DIM, TEXT_NO_BG, 1);
    display_flush();
}

/*
 * Idle screen. The prompt is drawn at one of several positions, cycling
 * every IDLE_MOVE_MS. This is an IPS LCD so true burn-in is not a risk,
 * but image retention is, and moving the text costs nothing.
 *
 * Wording is shorter than DESIGN.md 5.1's original ("Place a finger on
 * scanner, or press joystick for menu") specifically so it fits at scale
 * 2 -- at 12px/char, anything over ~19 characters runs off a 240px-wide
 * screen. DESIGN.md has been updated to match.
 */
static void screen_idle(int slot) {
    static const int ys[] = {30, 40, 50, 34, 46};
    static const int xs[] = {6, 14, 10, 4, 18};
    int n = sizeof(ys) / sizeof(ys[0]);
    int i = slot % n;

    display_clear(COL_BLACK);
    display_text(xs[i], ys[i], "Scan your finger",
                 COL_WHITE, TEXT_NO_BG, 2);
    display_text(xs[i], ys[i] + 22, "or SEL for menu",
                 COL_GREY, TEXT_NO_BG, 2);
    display_flush();
}

/* Full-screen colour flash before the detail screen -- a glance from
 * across the room is enough to tell match from no-match. Only used for
 * the passive idle-loop scan, not the menu-gate scan (that already reads
 * as a deliberate, watched action, not something you check in passing). */
static void flash_screen(uint16_t colour) {
    display_clear(colour);
    display_flush();
    sleep_ms(500);
}

/* password_desc == NULL means recognised but unbound (DESIGN.md 5.2). */
static void screen_result_match(const char *finger_name, const char *password_desc) {
    display_clear(COL_BLACK);
    display_text_centred(14, "MATCH", COL_GREEN, TEXT_NO_BG, 2);
    display_hline(20, 42, LCD_W - 40, COL_DIM);
    display_text_centred(50, finger_name, COL_WHITE, TEXT_NO_BG, 2);
    if (password_desc) {
        char line[40];
        snprintf(line, sizeof(line), "-> %s", password_desc);
        display_text_centred(78, line, COL_CYAN, TEXT_NO_BG, 1);
    } else {
        display_text_centred(78, "No password bound", COL_DIM, TEXT_NO_BG, 1);
    }
    display_flush();
}

static void screen_result_nomatch(uint32_t failed_attempts) {
    display_clear(COL_BLACK);
    display_text_centred(30, "NOT RECOGNISED", COL_RED, TEXT_NO_BG, 2);
    if (failed_attempts > 1) {
        char line[40];
        snprintf(line, sizeof(line), "%lu failed attempts",
                (unsigned long) failed_attempts);
        display_text_centred(64, line, COL_GREY, TEXT_NO_BG, 1);
    }
    display_flush();
}

static void screen_lockout(uint32_t seconds_left) {
    char line[40];
    display_clear(COL_BLACK);
    display_text_centred(24, "LOCKED", COL_RED, TEXT_NO_BG, 2);
    display_text_centred(56, "too many failures", COL_GREY, TEXT_NO_BG, 2);
    snprintf(line, sizeof(line), "wait %lu s", (unsigned long) seconds_left);
    display_text_centred(84, line, COL_YELLOW, TEXT_NO_BG, 2);
    display_flush();
}

static void screen_sensor_error(const char *why) {
    display_clear(COL_BLACK);
    display_text_centred(26, "SENSOR FAULT", COL_RED, TEXT_NO_BG, 2);
    display_text_centred(56, why, COL_GREY, TEXT_NO_BG, 1);
    display_text_centred(80, "check wiring and reset", COL_DIM,
                         TEXT_NO_BG, 1);
    display_flush();
}

static void screen_menu_gate(void) {
    display_clear(COL_BLACK);
    display_text_centred(44, "Scan enrolled", COL_WHITE, TEXT_NO_BG, 2);
    display_text_centred(64, "finger for menu", COL_WHITE, TEXT_NO_BG, 2);
    display_text_centred(100, "LEFT: cancel", COL_DIM, TEXT_NO_BG, 1);
    display_flush();
}

/* ------------------------------------------------------------------ */

/* Types the bound password while the match screen is already up, so
 * there's no dead pause -- DESIGN.md 5.2. The display holds for at least
 * RESULT_MS total; typing (bounded, ~50ms/char) happens inside that. */
static void handle_match(config_t *cfg, uint16_t sensor_id) {
    register_success(cfg);
    flash_screen(COL_GREEN);

    int finger_idx = config_finger_by_sensor_id(cfg, sensor_id);
    const char *name = (finger_idx >= 0)
        ? FINGER_NAMES[cfg->finger[finger_idx].name_index]
        : "(unknown finger)";
    int password_idx = (finger_idx >= 0) ? cfg->finger[finger_idx].password_index : -1;

    absolute_time_t t0 = get_absolute_time();
    screen_result_match(name, password_idx >= 0
                             ? cfg->password[password_idx].description : NULL);

    if (password_idx >= 0) {
        hid_type_string(cfg->password[password_idx].secret,
                        (host_layout_t) cfg->host_layout);
    }

    int64_t elapsed_ms = absolute_time_diff_us(t0, get_absolute_time()) / 1000;
    if (elapsed_ms < RESULT_MS) sleep_ms((uint32_t) (RESULT_MS - elapsed_ms));
}

/* Blocking (bounded by GATE_TIMEOUT_MS): opening the menu is a deliberate
 * act, unlike the passive idle scan, so freezing the joystick here is
 * fine -- there's nothing else to stay responsive for. */
static bool menu_gate_scan(config_t *cfg) {
    screen_menu_gate();
    fp_send(FP_CMD_COMPARE_1_N, 0, 0, 0);

    absolute_time_t deadline = make_timeout_time_ms(GATE_TIMEOUT_MS);
    while (!time_reached(deadline)) {
        hid_task();
        if (buttons_get() == BTN_LEFT) {
            fp_cancel();
            return false;
        }

        fp_reply_t r;
        fp_poll_t st = fp_poll(&r);
        if (st == FP_POLL_READY) {
            if (r.p3 >= 1 && r.p3 <= 3) {
                register_success(cfg);
                return true;
            }
            if (r.p3 == FP_ACK_TIMEOUT) {
                fp_send(FP_CMD_COMPARE_1_N, 0, 0, 0);   /* re-arm, keep waiting */
                continue;
            }
            register_failure(cfg);
            return false;
        }
        if (st == FP_POLL_BAD) return false;
        sleep_ms(5);
    }
    fp_cancel();
    return false;
}

/* ------------------------------------------------------------------ */

int main(void) {
    stdio_init_all();
    sleep_ms(200);

    display_init();
    buttons_init();
    hid_init();
    screen_splash();

    printf("\n%s v%s (%s)\n", FIRMWARE_NAME, FIRMWARE_VERSION, FIRMWARE_DATE);

    config_t cfg;
    config_load(&cfg);
    arm_lockout_from_count(cfg.failed_attempts);

    fp_init();

    uint16_t enrolled = 0;
    bool sensor_ok = fp_user_count(&enrolled);
    if (!sensor_ok) {
        printf("sensor did not answer user_count\n");
        screen_sensor_error("no reply from sensor");
        sleep_ms(4000);
    } else {
        printf("sensor OK, %u fingerprints enrolled\n", (unsigned) enrolled);
    }

    sleep_ms(800);

    int             idle_slot = 0;
    absolute_time_t next_move = make_timeout_time_ms(IDLE_MOVE_MS);
    absolute_time_t backlight_off_at = make_timeout_time_ms(BACKLIGHT_MS);
    bool            backlight_on = true;
    bool            searching = false;

    screen_idle(idle_slot);

    while (true) {
        hid_task();   /* service TinyUSB regardless of what else happens */

        /* ---- lockout takes precedence over everything ---- */
        uint32_t left;
        if (lockout_active(&left)) {
            if (searching) { fp_cancel(); searching = false; }
            screen_lockout(left);
            sleep_ms(250);
            continue;
        }

        /* ---- joystick ---- */
        button_t b = buttons_get();
        if (b != BTN_NONE) {
            if (!backlight_on) {
                display_backlight(true);
                backlight_on = true;
            }
            backlight_off_at = make_timeout_time_ms(BACKLIGHT_MS);

            if (b == BTN_SEL) {
                if (searching) { fp_cancel(); searching = false; }

                /* Bootstrap exception: with zero fingerprints enrolled,
                 * there is nothing to protect yet -- DESIGN.md 5.4. */
                if (cfg.finger_count == 0 || menu_gate_scan(&cfg)) {
                    menu_run(&cfg);
                }
                screen_idle(idle_slot);
            }
        }

        /* ---- fingerprint search, non-blocking ---- */
        if (sensor_ok && !searching) {
            fp_send(FP_CMD_COMPARE_1_N, 0, 0, 0);
            searching = true;
        }

        if (searching) {
            fp_reply_t r;
            fp_poll_t st = fp_poll(&r);

            if (st == FP_POLL_READY) {
                searching = false;

                if (!backlight_on) {
                    display_backlight(true);
                    backlight_on = true;
                }
                backlight_off_at = make_timeout_time_ms(BACKLIGHT_MS);

                if (r.p3 >= 1 && r.p3 <= 3) {
                    uint16_t id = (uint16_t) ((r.p1 << 8) | r.p2);
                    printf("match: id=%u privilege=%u\n",
                          (unsigned) id, (unsigned) r.p3);
                    handle_match(&cfg, id);
                    screen_idle(idle_slot);
                } else if (r.p3 == FP_ACK_TIMEOUT) {
                    /* module gave up waiting for a finger; not a failure */
                } else {
                    printf("no match (p3=0x%02X %s)\n", r.p3,
                          fp_ack_text(r.p3));
                    register_failure(&cfg);
                    flash_screen(COL_RED);
                    screen_result_nomatch(cfg.failed_attempts);
                    sleep_ms(RESULT_MS);
                    screen_idle(idle_slot);
                }
            } else if (st == FP_POLL_BAD) {
                searching = false;
                printf("bad frame from sensor\n");
            }
        }

        /* ---- idle text movement (image retention) ---- */
        if (time_reached(next_move)) {
            idle_slot++;
            next_move = make_timeout_time_ms(IDLE_MOVE_MS);
            if (backlight_on) screen_idle(idle_slot);
        }

        /* ---- backlight blanking ---- */
        if (backlight_on && time_reached(backlight_off_at)) {
            display_backlight(false);
            backlight_on = false;
        }

        sleep_ms(5);
    }
}
