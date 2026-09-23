/*
 * keyboard.h -- on-screen character grid for entering text with the
 * joystick (DESIGN.md 5.6/5.7)
 *
 * Pulled forward from milestone 4 into milestone 3: password/description
 * creation is otherwise untestable, since there is no other way to get
 * text into the device (WiFi entry is explicitly deferred, see DESIGN.md
 * section 9).
 *
 * The character set is deliberately small: a-z (case via SHIFT), 0-9,
 * and `. , - _ @`. Every character but '@' is layout-invariant -- see
 * CLAUDE.md. Do not add more punctuation to the grid.
 */
#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stddef.h>
#include <stdbool.h>

/* Edits `buf` (a NUL-terminated C string, may be empty) in place, up to
 * max_len - 1 characters. Shows `title` above the entry field. Returns
 * true and writes the result into `buf` if the user picked OK; returns
 * false and leaves `buf` untouched if they picked CANCEL. */
bool keyboard_edit(const char *title, char *buf, size_t max_len);

#endif /* KEYBOARD_H */
