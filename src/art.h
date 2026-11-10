/*
 * GBAR5 - the game's own pictures, downloaded on demand: the in-game
 * screenshot and the title screen (libretro-thumbnails, Game Boy Advance).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PSXS5_ART_H
#define PSXS5_ART_H

#include "library.h"
#include "platform/platform.h"

enum ArtKind
{
    ART_BEZEL, /* unused on GBAR5: a GBA picture is already 3:2 */
    ART_SNAP,  /* libretro-thumbnails: an in-game screenshot */
    ART_TITLE, /* libretro-thumbnails: the title screen */
    ART_KIND_COUNT
};

/* The game's picture, from <root>/art or downloaded (NULL until it's there,
 * or when there is none). Call it each frame the picture is shown. */
PlatTexture *art_get(const Game *g, int kind);
/* A download for it is waiting or running. */
bool art_pending(const Game *g, int kind);
/* Look a picture up ahead of time (the shelf warms its neighbours), so moving
 * the selection lands on a decoded texture. */
void art_warm(const Game *g, int kind);
/* The screenshot as a dimmed backdrop, at the picture's own small size: `keep`
 * of each channel survives (0..255) and the rest is `toward` (0xRRGGBB). The
 * shelf stretches it itself, which is far cheaper than scaling and blending it
 * across the screen every frame. NULL when the game has no picture. */
PlatTexture *art_wash(const Game *g, uint8_t keep, uint32_t toward);
/* The download setting changed: look every "no picture" up again. */
void art_refresh(void);

#endif
