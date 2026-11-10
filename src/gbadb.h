/*
 * GBAR5 - what the shelf knows about a Game Boy Advance cartridge: release
 * date, genres, developer, publisher, how many can play at once.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GBAR5_GBADB_H
#define GBAR5_GBADB_H

#include "gamedb.h" /* GameInfo: what the shelf shows about a game */
#include "library.h"

#include <stdbool.h>

/* Fills `out` from assets/gba-gamedb.txt (and <root>/gba-metadata.txt), by the
 * name of the file the cartridge came from and the title in its header.
 * False when neither list knows it - the shelf says "Unknown", as before. */
bool gbadb_get(const Game *g, GameInfo *out);

#endif
