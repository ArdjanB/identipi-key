/*
 * menu.h -- device menu, milestone 3 (DESIGN.md 5.5)
 *
 * Access to this must be fingerprint-gated by the caller (main.c), except
 * for the zero-fingers bootstrap case -- see DESIGN.md 5.4. This module
 * does not enforce that itself.
 */
#ifndef MENU_H
#define MENU_H

#include "config.h"

/* Runs the menu until the user backs out of the top level. Mutates and
 * saves *cfg as items are added, changed, or removed. */
void menu_run(config_t *cfg);

#endif /* MENU_H */
