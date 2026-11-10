/*
 * PSXS5 - interface icons (Tabler Icons subset).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PSXS5_ICONS_H
#define PSXS5_ICONS_H

#include "../psxs5.h"
#include "icons_list.h"

/* Draws icon (an ICON_* codepoint) centred in the size x size box at x, y. */
void icon_draw(int icon, float x, float y, float size, uint32_t argb);
/* Draws icon with the glyph's own left edge (its ink, not the size box) on x
 * and the ink's vertical centre on cy. Icons have a couple of pixels of side
 * bearing inside their box, so icon_draw() never lines up with text: a row
 * built from text and an icon needs this one to share the text's column. */
void icon_draw_ink_left(int icon, float x, float cy, float size, uint32_t argb);
void icons_shutdown(void);

#endif
