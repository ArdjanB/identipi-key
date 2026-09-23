/*
 * hid.c -- USB HID keyboard output, milestone 2
 *
 * Single-interface boot-protocol keyboard. No CDC, no vendor interface --
 * one USB personality, one job. See hid.h for the jitter rationale.
 */
#include "hid.h"

#include "tusb.h"
#include "pico/rand.h"
#include "pico/unique_id.h"
#include "pico/time.h"

#include <string.h>

#define KEY_HOLD_MS         10
#define GAP_MIN_MS          20
#define GAP_MAX_MS          40
#define HID_READY_TIMEOUT_MS 1000   /* abort typing if the host stalls */

/* ------------------------------------------------------------------ */
/* USB descriptors                                                     */
/* ------------------------------------------------------------------ */

#define USB_VID 0xCafeu   /* pid.codes test VID -- not a registered product */
#define USB_PID 0x4b45u

static tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0,
    .bDeviceSubClass    = 0,
    .bDeviceProtocol    = 0,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 1,
    .iProduct           = 2,
    .iSerialNumber      = 3,
    .bNumConfigurations = 1,
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *) &desc_device;
}

static uint8_t const desc_hid_report[] = {
    TUD_HID_REPORT_DESC_KEYBOARD()
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void) instance;
    return desc_hid_report;
}

enum { ITF_NUM_KEYBOARD = 0, ITF_NUM_TOTAL };
#define EPNUM_KEYBOARD    0x81
#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

static uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(ITF_NUM_KEYBOARD, 0, HID_ITF_PROTOCOL_KEYBOARD,
                       sizeof(desc_hid_report), EPNUM_KEYBOARD,
                       CFG_TUD_HID_EP_BUFSIZE, 10),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void) index;
    return desc_configuration;
}

enum { STRID_LANGID = 0, STRID_MANUFACTURER, STRID_PRODUCT, STRID_SERIAL };

static char serial_str[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];

static char const *string_desc[] = {
    NULL,
    "IdentiPi",
    "IdentiPi Key",
    serial_str,
};

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void) langid;
    static uint16_t desc_str[32 + 1];
    size_t chr_count;

    if (index == STRID_LANGID) {
        desc_str[1] = 0x0409;   /* English (US) */
        chr_count = 1;
    } else {
        if (index >= sizeof(string_desc) / sizeof(string_desc[0])) return NULL;
        const char *s = string_desc[index];
        chr_count = strlen(s);
        size_t max = (sizeof(desc_str) / sizeof(desc_str[0])) - 1;
        if (chr_count > max) chr_count = max;
        for (size_t i = 0; i < chr_count; i++) desc_str[1 + i] = (uint16_t) s[i];
    }

    desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return desc_str;
}

/* Neither callback has anything to do: no LEDs to drive (no GET_REPORT
 * feature, no OUTPUT report). Both are mandatory for the HID class driver
 * regardless. */
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen) {
    (void) instance; (void) report_id; (void) report_type;
    (void) buffer; (void) reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize) {
    (void) instance; (void) report_id; (void) report_type;
    (void) buffer; (void) bufsize;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

void hid_init(void) {
    pico_get_unique_board_id_string(serial_str, sizeof(serial_str));
    tusb_init();
}

void hid_task(void) {
    tud_task();
}

bool hid_connected(void) {
    return tud_mounted();
}

/* ------------------------------------------------------------------ */
/* Typing                                                              */
/* ------------------------------------------------------------------ */

static const uint8_t ascii_to_keycode[128][2] = { HID_ASCII_TO_KEYCODE };

/* Pumps tud_task() while waiting so the stack keeps servicing the host.
 * Bounded: an unplugged cable or a wedged host must not hang the device. */
static bool wait_hid_ready(void) {
    absolute_time_t deadline = make_timeout_time_ms(HID_READY_TIMEOUT_MS);
    while (!tud_hid_ready()) {
        tud_task();
        if (time_reached(deadline)) return false;
        sleep_us(500);
    }
    return true;
}

static void hid_sleep_ms(uint32_t ms) {
    absolute_time_t until = make_timeout_time_ms(ms);
    while (!time_reached(until)) {
        tud_task();
        sleep_us(500);
    }
}

static bool send_keycode(uint8_t modifier, uint8_t keycode) {
    uint8_t keys[6] = {0};
    keys[0] = keycode;
    if (!wait_hid_ready()) return false;
    return tud_hid_keyboard_report(0, modifier, keys);
}

static bool send_release(void) {
    if (!wait_hid_ready()) return false;
    return tud_hid_keyboard_report(0, 0, NULL);
}

#define SHIFT KEYBOARD_MODIFIER_LEFTSHIFT
#define ALTGR KEYBOARD_MODIFIER_RIGHTALT

/* UK matches the underlying US-keycode table for everything except these
 * three (DESIGN.md 5.7):
 *   '@': UK types this as Shift+' (apostrophe key), not Shift+2.
 *   '#': UK has a dedicated ISO key for this (HID_KEY_EUROPE_1, standard
 *        USB HID usage "Non-US # and ~"), unshifted.
 *   '\': UK's ISO layout has a separate dedicated key too
 *        (HID_KEY_EUROPE_2, "Non-US \ and |"), unshifted.
 * '@' was confirmed against real hardware in milestone 2; '#' and '\' are
 * standard/textbook mappings added with the expanded symbol grid, not
 * confirmed the same way -- test on a real UK host. */
static bool lookup_uk(char c, uint8_t *modifier, uint8_t *keycode) {
    switch (c) {
    case '@':  *modifier = SHIFT; *keycode = HID_KEY_APOSTROPHE; return true;
    case '#':  *modifier = 0;     *keycode = HID_KEY_EUROPE_1;   return true;
    case '\\': *modifier = 0;     *keycode = HID_KEY_EUROPE_2;   return true;
    default:   return false;
    }
}

/* DE (QWERTZ) diverges from the US table far more than UK does. None of
 * this has been confirmed against a real German host -- test thoroughly,
 * especially the punctuation, before trusting it with a real password.
 *
 *   Y/Z are swapped (physically, not just relabelled): the scancode
 *   traditionally called "Y" sits where a German keyboard has Z, and
 *   vice versa. Two characters, two keys, just crossed.
 *
 *   Most of the digit-row shift positions and bottom-row punctuation
 *   live on different keys entirely -- not merely different modifiers.
 *   '!', '$', '%', '.', ',' happen to land in the same place as the US
 *   table already gives us and need no override.
 */
static bool lookup_de(char c, uint8_t *modifier, uint8_t *keycode) {
    switch (c) {
    case '@':  *modifier = ALTGR; *keycode = HID_KEY_Q;           return true;
    case '#':  *modifier = 0;     *keycode = HID_KEY_EUROPE_1;    return true;
    case '\\': *modifier = ALTGR; *keycode = HID_KEY_MINUS;       return true;
    case 'y':  *modifier = 0;     *keycode = HID_KEY_Z;           return true;
    case 'Y':  *modifier = SHIFT; *keycode = HID_KEY_Z;           return true;
    case 'z':  *modifier = 0;     *keycode = HID_KEY_Y;           return true;
    case 'Z':  *modifier = SHIFT; *keycode = HID_KEY_Y;           return true;
    case '&':  *modifier = SHIFT; *keycode = HID_KEY_6;           return true;
    case '(':  *modifier = SHIFT; *keycode = HID_KEY_8;           return true;
    case ')':  *modifier = SHIFT; *keycode = HID_KEY_9;           return true;
    case '=':  *modifier = SHIFT; *keycode = HID_KEY_0;           return true;
    /* German row 2 has TWO extra keys after P: U DOT (Ue-umlaut) sits at
     * the US '[' position, then '+' at the US ']' position -- one slot
     * further right than it looks from the "key after P" description.
     * Off-by-one here previously sent '+'/'*' to the Ü key instead,
     * confirmed by real-hardware testing (typing '+' produced 'u"'). */
    case '+':  *modifier = 0;     *keycode = HID_KEY_BRACKET_RIGHT; return true;
    case '*':  *modifier = SHIFT; *keycode = HID_KEY_BRACKET_RIGHT; return true;
    case '?':  *modifier = SHIFT; *keycode = HID_KEY_MINUS;       return true;
    case '-':  *modifier = 0;     *keycode = HID_KEY_SLASH;       return true;
    case '_':  *modifier = SHIFT; *keycode = HID_KEY_SLASH;       return true;
    case '/':  *modifier = SHIFT; *keycode = HID_KEY_7;           return true;
    case '<':  *modifier = 0;     *keycode = HID_KEY_EUROPE_2;    return true;
    case '>':  *modifier = SHIFT; *keycode = HID_KEY_EUROPE_2;    return true;
    case ':':  *modifier = SHIFT; *keycode = HID_KEY_PERIOD;      return true;
    case ';':  *modifier = SHIFT; *keycode = HID_KEY_COMMA;       return true;
    default:   return false;
    }
}

/* Returns false if the host stopped responding -- caller aborts the rest
 * of the string in that case. */
static bool type_char(char c, host_layout_t layout) {
    uint8_t modifier, keycode;

    bool special = (layout == HOST_LAYOUT_DE)
        ? lookup_de(c, &modifier, &keycode)
        : lookup_uk(c, &modifier, &keycode);

    if (!special) {
        if ((unsigned char) c < 128) {
            modifier = ascii_to_keycode[(unsigned char) c][0]
                       ? KEYBOARD_MODIFIER_LEFTSHIFT : 0;
            keycode  = ascii_to_keycode[(unsigned char) c][1];
        } else {
            return true;   /* not representable on a keyboard; skip it */
        }
    }

    if (keycode == 0) return true;

    if (!send_keycode(modifier, keycode)) return false;
    hid_sleep_ms(KEY_HOLD_MS);
    if (!send_release()) return false;
    return true;
}

void hid_type_string(const char *s, host_layout_t layout) {
    if (!tud_mounted()) return;

    for (const char *p = s; *p; p++) {
        if (!type_char(*p, layout)) return;
        if (p[1] != '\0') {
            uint32_t gap = GAP_MIN_MS + (get_rand_32() % (GAP_MAX_MS - GAP_MIN_MS + 1));
            hid_sleep_ms(gap);
        }
    }
}
