/*
 * GBAR5 - the in-game overlay's card, shared with the screens it opens.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PSXS5_DECK_H
#define PSXS5_DECK_H

/* The card, its head, and the body's top and bottom (1920x1080 pixels). */
void deck_draw(float a);
void deck_draw_header(float a);
float deck_top(void);
float deck_bottom(void);

#endif
