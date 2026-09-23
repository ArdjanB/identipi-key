/*
 * menu.c -- device menu, milestones 3 and 5 (DESIGN.md 5.5)
 */
#include "menu.h"
#include "config.h"
#include "keyboard.h"
#include "display.h"
#include "buttons.h"
#include "fp.h"
#include "hid.h"
#include "otp_key.h"
#include "version.h"

#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Small screen helpers shared by every menu item                      */
/* ------------------------------------------------------------------ */

/* columns == 1: the original scrolling single-column list (5 rows visible,
 * windowed around the cursor). `divider_after[i]` (may be NULL), if true,
 * draws a thin rule below item i -- used to group the top-level MENU into
 * "fingerprint items" / "password items" / "everything else".
 *
 * columns == 2: a fixed grid sized to fit the whole list at once (used for
 * the <=10-item finger-name lists, where scrolling would be more friction
 * than it's worth). `item_col[i]` (required, 0 or 1) says which column
 * item i belongs in -- e.g. left-hand vs right-hand finger names -- rather
 * than splitting the list positionally in half. That distinction matters
 * once the two sides have different counts (a left-hand finger already
 * enrolled): a positional split would let a right-hand name bleed into
 * the left column to keep the halves even, which is exactly wrong.
 *
 * Neither mode wraps top-to-bottom: UP at the first row and DOWN at the
 * last just hold still. LEFT still backs out of the whole picker, from
 * either column. */
static int select_from_list(const char *title, const char *const *items,
                            int count, int start_idx, int columns,
                            const int *item_col, const bool *divider_after) {
    if (count <= 0) return -1;
    int idx = start_idx;
    if (idx < 0 || idx >= count) idx = 0;

    int col_items[2][FINGER_NAME_COUNT];
    int col_of[FINGER_NAME_COUNT], row_of[FINGER_NAME_COUNT];
    int col_count[2] = {0, 0};
    int col_w = LCD_W / 2;
    if (columns == 2) {
        for (int i = 0; i < count; i++) {
            int c = item_col[i];
            row_of[i] = col_count[c];
            col_of[i] = c;
            col_items[c][col_count[c]] = i;
            col_count[c]++;
        }
    }

    while (true) {
        display_clear(COL_BLACK);
        display_text_centred(2, title, COL_CYAN, TEXT_NO_BG, 1);
        display_hline(0, 14, LCD_W, COL_DIM);

        if (columns == 1) {
            /* Scale 2: this is what you're actually reading to make a
             * choice. A description right at DESC_LEN's limit can clip
             * at the right edge (display_fill_rect clamps safely, so
             * that's cosmetic, never a crash) -- an acceptable trade for
             * everything shorter reading clearly. */
            const int visible = 5;
            int top = idx - visible / 2;
            if (top > count - visible) top = count - visible;
            if (top < 0) top = 0;

            int y = 18;
            for (int row = 0; row < visible && (top + row) < count; row++) {
                int i = top + row;
                bool sel = (i == idx);
                if (sel) display_fill_rect(1, y - 2, LCD_W - 2, 20, COL_CYAN);
                display_text(6, y, items[i], sel ? COL_BLACK : COL_WHITE,
                            TEXT_NO_BG, 2);
                y += 20;
                if (divider_after && divider_after[i]) {
                    display_hline(4, y, LCD_W - 8, COL_DIM);
                    y += 6;
                }
            }
        } else {
            for (int c = 0; c < 2; c++) {
                for (int r = 0; r < col_count[c]; r++) {
                    int i = col_items[c][r];
                    int x = c * col_w;
                    int y = 18 + r * 18;
                    bool sel = (i == idx);
                    if (sel) display_fill_rect(x + 1, y - 2, col_w - 2, 16, COL_CYAN);
                    display_text(x + 4, y, items[i], sel ? COL_BLACK : COL_WHITE,
                                TEXT_NO_BG, 1);
                }
            }
        }

        display_text_centred(126, "SEL choose  LEFT back", COL_DIM,
                             TEXT_NO_BG, 1);
        display_flush();

        button_t b = buttons_wait_repeat(BTN_MASK_ANY, 0);
        if (columns == 1) {
            if (b == BTN_UP)        { if (idx > 0) idx--; }
            else if (b == BTN_DOWN) { if (idx < count - 1) idx++; }
            else if (b == BTN_SEL)  return idx;
            else if (b == BTN_LEFT) return -1;
            continue;
        }

        /* 2-D navigation over the grouped grid. LEFT steps toward column
         * 0 and, once there, backs out -- same button, no extra cell
         * needed, consistent with "LEFT always eventually backs out"
         * everywhere else in the menu. */
        int col = col_of[idx];
        int row = row_of[idx];

        if (b == BTN_UP) {
            if (row > 0) idx = col_items[col][row - 1];
        } else if (b == BTN_DOWN) {
            if (row < col_count[col] - 1) idx = col_items[col][row + 1];
        } else if (b == BTN_RIGHT) {
            if (col == 0 && col_count[1] > 0) {
                int r = (row >= col_count[1]) ? col_count[1] - 1 : row;
                idx = col_items[1][r];
            }
        } else if (b == BTN_LEFT) {
            if (col == 1) {
                int r = (row >= col_count[0]) ? col_count[0] - 1 : row;
                idx = col_items[0][r];
            } else {
                return -1;
            }
        } else if (b == BTN_SEL) {
            return idx;
        }
    }
}

static bool confirm(const char *line1, const char *line2) {
    display_clear(COL_BLACK);
    display_text_centred(38, line1, COL_WHITE, TEXT_NO_BG, 2);
    if (line2) display_text_centred(60, line2, COL_GREY, TEXT_NO_BG, 1);
    display_text_centred(94, "SEL: yes   LEFT: no", COL_YELLOW, TEXT_NO_BG, 1);
    display_flush();
    return buttons_wait(BTN_MASK_ANY, 0) == BTN_SEL;
}

static void message(const char *line1, const char *line2) {
    display_clear(COL_BLACK);
    display_text_centred(50, line1, COL_WHITE, TEXT_NO_BG, 2);
    if (line2) display_text_centred(68, line2, COL_GREY, TEXT_NO_BG, 1);
    display_flush();
    buttons_wait(BTN_MASK_ANY, 3000);
}

/* config_save()'s result was previously discarded everywhere, so a real
 * flash-write failure looked identical to a UI slip (silently nothing
 * saved). Surface it instead. */
static void save_or_warn(config_t *cfg) {
    if (!config_save(cfg)) {
        message("SAVE FAILED", "flash write did not verify");
    }
}

static const char *layout_name(uint8_t host_layout) {
    return host_layout == HOST_LAYOUT_DE ? "DE" : "UK";
}

/* Fills out_idx with the slot index of every enrolled finger, ordered by
 * name_index -- Left Thumb..Left Pinky, then Right Thumb..Right Pinky --
 * and returns how many. A config slot number is just "the first free
 * slot at enrollment time" and has no relationship to which finger it
 * holds, so without this, fingers list in whatever order they happened
 * to be enrolled rather than the anatomical left-to-right order the
 * enroll picker already uses (it iterates FINGER_NAMES directly). Used
 * anywhere fingers are listed: Info, Info export, and the Delete/Assign
 * pickers, so all four agree with each other and with Enroll. */
static int sorted_finger_indices(const config_t *cfg, int *out_idx) {
    int n = 0;
    for (int name_idx = 0; name_idx < FINGER_NAME_COUNT; name_idx++) {
        for (int i = 0; i < MAX_FINGERS; i++) {
            if (cfg->finger[i].in_use && cfg->finger[i].name_index == name_idx) {
                out_idx[n++] = i;
                break;
            }
        }
    }
    return n;
}

/* Fills out_idx with the slot index of every in-use password, sorted
 * alphabetically by description, and returns how many. Every list of
 * passwords shown to the user goes through this, so they don't have to
 * hunt in whatever order slots happened to fill in. Plain insertion sort
 * -- at most MAX_PASSWORDS(32) entries, no need for anything cleverer. */
static int sorted_password_indices(const config_t *cfg, int *out_idx) {
    int n = 0;
    for (int i = 0; i < MAX_PASSWORDS; i++)
        if (cfg->password[i].in_use) out_idx[n++] = i;

    for (int i = 1; i < n; i++) {
        int key = out_idx[i];
        int j = i - 1;
        while (j >= 0 && strcmp(cfg->password[out_idx[j]].description,
                                cfg->password[key].description) > 0) {
            out_idx[j + 1] = out_idx[j];
            j--;
        }
        out_idx[j + 1] = key;
    }
    return n;
}

/* Shared by enrollment's "bind to password" step and the standalone
 * "Assign password" menu item. `current` (a password index, or -1) is
 * pre-selected in the list if it's still a valid, in-use password.
 * Returns the chosen password index, -1 for "(no password)" explicitly
 * picked, or -2 if the user backed out without choosing anything. */
static int pick_password(config_t *cfg, int current) {
    const char *items[MAX_PASSWORDS + 1];
    int idx_map[MAX_PASSWORDS + 1];
    int order[MAX_PASSWORDS];
    int n = sorted_password_indices(cfg, order);

    int m = 0;
    items[m] = "(no password)";
    idx_map[m] = -1;
    int start = 0;
    m++;
    for (int k = 0; k < n; k++) {
        items[m] = cfg->password[order[k]].description;
        idx_map[m] = order[k];
        if (order[k] == current) start = m;
        m++;
    }
    int sel = select_from_list("Bind to password", items, m, start, 1, NULL, NULL);
    if (sel < 0) return -2;
    return idx_map[sel];
}

/* ------------------------------------------------------------------ */
/* 1. Enroll fingerprint                                               */
/* ------------------------------------------------------------------ */

static void menu_enroll(config_t *cfg) {
    int finger_idx = config_find_free_finger(cfg);
    if (finger_idx < 0) {
        message("All 10 fingers", "are already enrolled");
        return;
    }

    const char *names[FINGER_NAME_COUNT];
    int name_map[FINGER_NAME_COUNT];
    int name_col[FINGER_NAME_COUNT];   /* 0 = Left..., 1 = Right... */
    int n = 0;
    for (int i = 0; i < FINGER_NAME_COUNT; i++) {
        if (!config_name_in_use(cfg, (uint8_t) i)) {
            names[n] = FINGER_NAMES[i];
            name_map[n] = i;
            name_col[n] = (i < 5) ? 0 : 1;
            n++;
        }
    }
    if (n == 0) return;   /* can't happen: 10 names, 10 slots */

    int pick = select_from_list("Enroll: choose finger", names, n, 0, 2, name_col, NULL);
    if (pick < 0) return;
    uint8_t name_index = (uint8_t) name_map[pick];
    uint16_t sensor_id = (uint16_t) (finger_idx + 1);

    /* CONFIRMED against SB Components' own Demo_Add_Fingerprint.py /
     * IdentiPi.py (add_fingerprint()): the frame is CMD, P1, P2, P3, 00,
     * where P1:P2 is the 2-byte ID (matching DELETE_USER/COMPARE_1_N) and
     * P3 is the privilege level -- their docstring is explicit that "p3 -
     * must be Non-zero value". This lines up with CLAUDE.md's already-
     * verified fact that COMPARE_1_N returns privilege 1-3 in P3 on a
     * match: ADD is where that privilege gets assigned. We had been
     * sending P3=0, which the sensor silently accepted (ack=00) without
     * ever storing a findable template -- that's the whole story behind
     * "3x ack=00" followed by "not found anywhere". This design attaches
     * no meaning to privilege level itself, so a fixed value in range is
     * enough. */
    const uint8_t privilege = 1;
    for (int step = 1; step <= 3; step++) {
        char line[24];
        snprintf(line, sizeof(line), "Scan %d of 3", step);
        display_clear(COL_BLACK);
        display_text_centred(40, "Place finger on scanner", COL_WHITE,
                             TEXT_NO_BG, 1);
        display_text_centred(60, line, COL_CYAN, TEXT_NO_BG, 1);
        display_text_centred(100, "LEFT: cancel", COL_DIM, TEXT_NO_BG, 1);
        display_flush();

        uint8_t cmd = step == 1 ? FP_CMD_ADD_1 : step == 2 ? FP_CMD_ADD_2 : FP_CMD_ADD_3;
        fp_send(cmd, (uint8_t) (sensor_id >> 8), (uint8_t) (sensor_id & 0xFF), privilege);

        bool ok = false, cancelled = false, bad_frame = false;
        uint8_t last_ack = 0xFF;
        absolute_time_t deadline = make_timeout_time_ms(10000);
        while (!time_reached(deadline)) {
            hid_task();
            if (buttons_get() == BTN_LEFT) {
                fp_cancel();
                cancelled = true;
                break;
            }
            fp_reply_t r;
            fp_poll_t st = fp_poll(&r);
            if (st == FP_POLL_READY) {
                last_ack = r.p3;
                ok = (r.p3 == FP_ACK_SUCCESS);
                break;
            }
            if (st == FP_POLL_BAD) { bad_frame = true; break; }
            sleep_ms(5);
        }

        if (!ok || cancelled) {
            fp_cancel();
            fp_delete_user(sensor_id);   /* best-effort: clear any partial template */
            if (!cancelled) {
                char detail[32];
                if (bad_frame) snprintf(detail, sizeof(detail), "bad frame from sensor");
                else snprintf(detail, sizeof(detail), "sensor: %s", fp_ack_text(last_ack));
                message("Enrollment failed", detail);
            }
            return;
        }
        if (step < 3) message("Lift finger,", "then place again");
    }

    int8_t password_index = -1;
    if (cfg->password_count > 0) {
        int chosen = pick_password(cfg, -1);
        if (chosen != -2) password_index = (int8_t) chosen;
    }

    config_add_finger(cfg, sensor_id, name_index, password_index);
    save_or_warn(cfg);
    message("Enrolled", FINGER_NAMES[name_index]);
}

/* ------------------------------------------------------------------ */
/* 2. Delete fingerprint                                               */
/* ------------------------------------------------------------------ */

static void menu_delete_finger(config_t *cfg) {
    if (cfg->finger_count == 0) {
        message("No fingers", "enrolled yet");
        return;
    }

    const char *items[MAX_FINGERS];
    int idx_map[MAX_FINGERS];
    int item_col[MAX_FINGERS];
    int order[MAX_FINGERS];
    int n = sorted_finger_indices(cfg, order);
    for (int k = 0; k < n; k++) {
        int i = order[k];
        items[k] = FINGER_NAMES[cfg->finger[i].name_index];
        idx_map[k] = i;
        item_col[k] = (cfg->finger[i].name_index < 5) ? 0 : 1;
    }

    int sel = select_from_list("Delete fingerprint", items, n, 0, 2, item_col, NULL);
    if (sel < 0) return;
    int finger_idx = idx_map[sel];
    const char *name = FINGER_NAMES[cfg->finger[finger_idx].name_index];

    if (!confirm("Delete this finger?", name)) return;

    fp_delete_user(cfg->finger[finger_idx].sensor_id);
    config_delete_finger(cfg, finger_idx);
    save_or_warn(cfg);
    message("Deleted", name);
}

/* ------------------------------------------------------------------ */
/* 3. Create password                                                  */
/* ------------------------------------------------------------------ */

static void menu_create_password(config_t *cfg) {
    if (cfg->password_count >= MAX_PASSWORDS) {
        message("All password slots", "are in use");
        return;
    }

    char description[DESC_LEN] = "";
    if (!keyboard_edit("Description (1/2)", description, sizeof(description))) return;

    char secret[SECRET_LEN] = "";
    if (!keyboard_edit("Password (2/2)", secret, sizeof(secret))) return;

    config_add_password(cfg, description, secret);
    save_or_warn(cfg);
    message("Password saved", description);
}

/* ------------------------------------------------------------------ */
/* 4. Change password                                                  */
/* ------------------------------------------------------------------ */

static void menu_change_password(config_t *cfg) {
    if (cfg->password_count == 0) {
        message("No passwords", "created yet");
        return;
    }

    const char *items[MAX_PASSWORDS];
    int idx_map[MAX_PASSWORDS];
    int order[MAX_PASSWORDS];
    int n = sorted_password_indices(cfg, order);
    for (int k = 0; k < n; k++) {
        items[k] = cfg->password[order[k]].description;
        idx_map[k] = order[k];
    }

    int sel = select_from_list("Change password", items, n, 0, 1, NULL, NULL);
    if (sel < 0) return;
    int idx = idx_map[sel];

    char description[DESC_LEN];
    strncpy(description, cfg->password[idx].description, sizeof(description) - 1);
    description[sizeof(description) - 1] = '\0';
    if (!keyboard_edit("Description (1/2)", description, sizeof(description))) return;

    char secret[SECRET_LEN];
    strncpy(secret, cfg->password[idx].secret, sizeof(secret) - 1);
    secret[sizeof(secret) - 1] = '\0';
    if (!keyboard_edit("Password (2/2)", secret, sizeof(secret))) return;

    strncpy(cfg->password[idx].description, description, DESC_LEN - 1);
    cfg->password[idx].description[DESC_LEN - 1] = '\0';
    strncpy(cfg->password[idx].secret, secret, SECRET_LEN - 1);
    cfg->password[idx].secret[SECRET_LEN - 1] = '\0';
    save_or_warn(cfg);
    message("Updated", description);
}

/* ------------------------------------------------------------------ */
/* 5. Delete password                                                  */
/* ------------------------------------------------------------------ */

static void menu_delete_password(config_t *cfg) {
    if (cfg->password_count == 0) {
        message("No passwords", "created yet");
        return;
    }

    const char *items[MAX_PASSWORDS];
    int idx_map[MAX_PASSWORDS];
    int order[MAX_PASSWORDS];
    int n = sorted_password_indices(cfg, order);
    for (int k = 0; k < n; k++) {
        items[k] = cfg->password[order[k]].description;
        idx_map[k] = order[k];
    }

    int sel = select_from_list("Delete password", items, n, 0, 1, NULL, NULL);
    if (sel < 0) return;
    int idx = idx_map[sel];

    if (!confirm("Delete this password?", cfg->password[idx].description)) return;

    config_delete_password(cfg, idx);
    save_or_warn(cfg);
    message("Deleted", "bound fingers are now unbound");
}

/* ------------------------------------------------------------------ */
/* 6. Assign password to fingerprint                                   */
/* ------------------------------------------------------------------ */

/* Move a password to a different finger, re-bind after deleting one, or
 * unassign a finger entirely -- without re-enrolling or re-creating
 * anything. */
static void menu_assign_password(config_t *cfg) {
    if (cfg->finger_count == 0) {
        message("No fingers", "enrolled yet");
        return;
    }

    const char *items[MAX_FINGERS];
    int idx_map[MAX_FINGERS];
    int item_col[MAX_FINGERS];
    int order[MAX_FINGERS];
    int n = sorted_finger_indices(cfg, order);
    for (int k = 0; k < n; k++) {
        int i = order[k];
        items[k] = FINGER_NAMES[cfg->finger[i].name_index];
        idx_map[k] = i;
        item_col[k] = (cfg->finger[i].name_index < 5) ? 0 : 1;
    }

    int sel = select_from_list("Assign: choose finger", items, n, 0, 2, item_col, NULL);
    if (sel < 0) return;
    int finger_idx = idx_map[sel];

    int chosen = pick_password(cfg, cfg->finger[finger_idx].password_index);
    if (chosen == -2) return;

    cfg->finger[finger_idx].password_index = (int8_t) chosen;
    save_or_warn(cfg);
    message("Assignment updated", FINGER_NAMES[cfg->finger[finger_idx].name_index]);
}

/* ------------------------------------------------------------------ */
/* 7. Info                                                             */
/* ------------------------------------------------------------------ */

static void menu_info(config_t *cfg) {
    display_clear(COL_BLACK);
    display_text_centred(2, "INFO", COL_CYAN, TEXT_NO_BG, 1);
    display_hline(0, 12, LCD_W, COL_DIM);

    char line[40];
    snprintf(line, sizeof(line), "%s v%s", FIRMWARE_NAME, FIRMWARE_VERSION);
    display_text(2, 15, line, COL_WHITE, TEXT_NO_BG, 1);
    snprintf(line, sizeof(line), "built %s", FIRMWARE_BUILD_TIME);
    display_text(2, 25, line, COL_GREY, TEXT_NO_BG, 1);
    snprintf(line, sizeof(line), "%u fingers, %u passwords",
             (unsigned) cfg->finger_count, (unsigned) cfg->password_count);
    display_text(2, 35, line, COL_GREY, TEXT_NO_BG, 1);
    snprintf(line, sizeof(line), "Encryption: %s",
            otp_key_is_provisioned() ? "OTP key set" : "not provisioned");
    display_text(2, 45, line, COL_GREY, TEXT_NO_BG, 1);
    display_hline(0, 56, LCD_W, COL_DIM);

    int y = 60;
    int order[MAX_FINGERS];
    int total = sorted_finger_indices(cfg, order);
    int shown = 0;
    for (int k = 0; k < total && shown < 5; k++) {
        int i = order[k];
        const char *pw = cfg->finger[i].password_index >= 0
            ? cfg->password[cfg->finger[i].password_index].description
            : "(none)";
        snprintf(line, sizeof(line), "%s -> %s",
                FINGER_NAMES[cfg->finger[i].name_index], pw);
        display_text(2, y, line, COL_WHITE, TEXT_NO_BG, 1);
        y += 10;
        shown++;
    }
    if (shown == 0)
        display_text(2, y, "(no fingers enrolled)", COL_DIM, TEXT_NO_BG, 1);

    display_text_centred(122, "any key: back", COL_DIM, TEXT_NO_BG, 1);
    display_flush();
    buttons_wait(BTN_MASK_ANY, 0);
}

/* ------------------------------------------------------------------ */
/* 8. Info export                                                      */
/* ------------------------------------------------------------------ */

/* Types the same finger->password mapping the Info screen shows, plus
 * firmware version, into whatever the host has focused -- the device has
 * no other output channel. Descriptions only, never the secrets
 * themselves: unlike a single password typed on a real match, this would
 * dump everything at once into whatever happens to be focused. */
static void menu_info_export(config_t *cfg) {
    if (!hid_connected()) {
        message("Not connected", "plug in the USB cable first");
        return;
    }

    display_clear(COL_BLACK);
    display_text_centred(36, "Exporting to editor...", COL_YELLOW, TEXT_NO_BG, 1);
    display_text_centred(52, "click into a text editor", COL_WHITE, TEXT_NO_BG, 1);
    display_text_centred(64, "before this finishes", COL_WHITE, TEXT_NO_BG, 1);
    display_text_centred(88, "LEFT: cancel", COL_DIM, TEXT_NO_BG, 1);
    display_flush();

    /* Give the user a moment to click into an editor before typing starts. */
    for (int i = 0; i < 30; i++) {
        if (buttons_get() == BTN_LEFT) return;
        sleep_ms(100);
    }

    /* Generously sized: worst case is a 12-char name + " -> " + a
     * 23-char description per finger, times 10 fingers, plus a short
     * header -- under 500 bytes even at DESIGN.md's stated capacities. */
    char report[768];
    size_t len = 0;
    len += (size_t) snprintf(report + len, sizeof(report) - len,
                             "%s v%s (built %s)\n", FIRMWARE_NAME,
                             FIRMWARE_VERSION, FIRMWARE_BUILD_TIME);
    len += (size_t) snprintf(report + len, sizeof(report) - len,
                             "%u fingers, %u passwords\n",
                             (unsigned) cfg->finger_count,
                             (unsigned) cfg->password_count);
    len += (size_t) snprintf(report + len, sizeof(report) - len,
                             "Keyboard layout: %s\n", layout_name(cfg->host_layout));
    len += (size_t) snprintf(report + len, sizeof(report) - len,
                             "Encryption: %s\n\n",
                             otp_key_is_provisioned() ? "OTP key set" : "not provisioned");

    int order[MAX_FINGERS];
    int total = sorted_finger_indices(cfg, order);
    int shown = 0;
    for (int k = 0; k < total && len < sizeof(report); k++) {
        int i = order[k];
        const char *pw = cfg->finger[i].password_index >= 0
            ? cfg->password[cfg->finger[i].password_index].description
            : "(none)";
        len += (size_t) snprintf(report + len, sizeof(report) - len, "%s -> %s\n",
                                 FINGER_NAMES[cfg->finger[i].name_index], pw);
        shown++;
    }
    if (shown == 0 && len < sizeof(report)) {
        len += (size_t) snprintf(report + len, sizeof(report) - len,
                                 "(no fingers enrolled)\n");
    }
    /* Trailing blank line so the cursor lands clearly past the report,
     * not immediately after the last character of content. */
    if (len < sizeof(report)) {
        len += (size_t) snprintf(report + len, sizeof(report) - len, "\n");
    }

    hid_type_string(report, (host_layout_t) cfg->host_layout);
    message("Export done", NULL);
}

/* ------------------------------------------------------------------ */
/* 9. Keyboard layout                                                  */
/* ------------------------------------------------------------------ */

static void menu_layout(config_t *cfg) {
    static const char *const items[2] = { "UK", "DE" };
    int sel = select_from_list("Host keyboard layout", items, 2, cfg->host_layout, 1, NULL, NULL);
    if (sel < 0) return;
    cfg->host_layout = (uint8_t) sel;
    save_or_warn(cfg);
    message("Layout set", items[sel]);
}

/* ------------------------------------------------------------------ */
/* 10. Provision encryption key                                        */
/* ------------------------------------------------------------------ */

/* Milestone 5, phase 1. Generates the config-blob AES-256 key from the
 * hardware TRNG and burns it to OTP -- entirely on-device, so the key
 * value never touches a host, a file, or this or any other chat session
 * (CLAUDE.md's rule on this is absolute). This menu item is the
 * deliberate act the user takes with the board in hand; the code path
 * itself only runs when they choose to trigger it, same spirit as OTP
 * fuse writes anywhere else in this project.
 *
 * This does NOT make the key secret yet -- that also needs debug-disable
 * and the OTP page lock bits (both still fully manual, later, per
 * DESIGN.md 7's provisioning order). Until then this only tests the
 * mechanism: does the encrypt/decrypt round trip work correctly. */
static void menu_provision_key(config_t *cfg) {
    (void) cfg;

    if (otp_key_is_provisioned()) {
        message("Encryption key", "already provisioned");
        return;
    }

    if (!confirm("Provision encryption key?", "This is PERMANENT")) return;
    if (!confirm("Really sure?", "OTP writes cannot be undone")) return;

    display_clear(COL_BLACK);
    display_text_centred(50, "Provisioning...", COL_YELLOW, TEXT_NO_BG, 2);
    display_flush();

    if (otp_key_provision()) {
        message("Key provisioned", "next save will encrypt");
    } else {
        message("PROVISIONING FAILED", "check picotool otp list");
    }
}

/* ------------------------------------------------------------------ */

void menu_run(config_t *cfg) {
    static const char *const labels[] = {
        "Enroll fingerprint", "Delete fingerprint",   "Create password",
        "Change password",    "Delete password",      "Assign password",
        "Info",               "Info export",           "Keyboard layout",
        "Provision encryption key",
    };
    const int count = sizeof(labels) / sizeof(labels[0]);
    /* Group into fingerprint items / password items / everything else. */
    static const bool divider_after[] = {
        false, true, false, false, false, true, false, false, false, false,
    };

    int idx = 0;
    while (true) {
        /* Keyboard layout's line shows the current setting -- rebuilt
         * every pass since it can change from inside the menu itself. */
        const char *items[sizeof(labels) / sizeof(labels[0])];
        for (size_t i = 0; i < sizeof(labels) / sizeof(labels[0]); i++) items[i] = labels[i];
        char layout_item[24];
        snprintf(layout_item, sizeof(layout_item), "Keyboard layout: %s",
                layout_name(cfg->host_layout));
        items[8] = layout_item;

        int sel = select_from_list("MENU", items, count, idx, 1, NULL, divider_after);
        if (sel < 0) return;
        idx = sel;

        switch (sel) {
        case 0: menu_enroll(cfg); break;
        case 1: menu_delete_finger(cfg); break;
        case 2: menu_create_password(cfg); break;
        case 3: menu_change_password(cfg); break;
        case 4: menu_delete_password(cfg); break;
        case 5: menu_assign_password(cfg); break;
        case 6: menu_info(cfg); break;
        case 7: menu_info_export(cfg); break;
        case 8: menu_layout(cfg); break;
        case 9: menu_provision_key(cfg); break;
        }
    }
}
