/*
 * hid.h -- USB HID keyboard output, milestone 2
 *
 * Boot-protocol keyboard (recognised even by BIOS/pre-boot environments,
 * not just OS HID drivers). This is the only USB interface the device
 * exposes -- the fingerprint sensor and its state are never reachable
 * over USB in any form.
 *
 * Fixed 10 ms key hold, jittered 20-40 ms inter-key gap. Only the gap is
 * jittered: a randomly varying hold can trip auto-repeat on some hosts and
 * double characters, and a fixed cadence is a signature for anything
 * sampling USB timing.
 *
 * Only UK and DE are supported (see DESIGN.md 5.7). UK differs from the
 * underlying US-keycode table only for '@', '#', and '\'. DE (QWERTZ)
 * differs far more: Y and Z are swapped, and most of the shifted
 * digit-row and bottom-row punctuation in the on-screen keyboard's symbol
 * page lives on entirely different keys.
 */
#ifndef HID_H
#define HID_H

#include <stdbool.h>

typedef enum {
    HOST_LAYOUT_UK = 0,
    HOST_LAYOUT_DE = 1,
} host_layout_t;

#define HOST_LAYOUT_DEFAULT HOST_LAYOUT_DE

void hid_init(void);
void hid_task(void);          /* pumps TinyUSB -- call every main-loop pass */
bool hid_connected(void);

/* Blocking, bounded by roughly strlen(s) * 50 ms. Gives up silently on a
 * character (or the rest of the string) if the host stops responding, so
 * an unplugged cable mid-type cannot hang the device. */
void hid_type_string(const char *s, host_layout_t layout);

#endif /* HID_H */
