/*
 * GBAR5 - the Game Boy Advance's own startup, drawn from scratch.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The real animation comes out of Nintendo's boot ROM, so it only exists when
 * the player has a gba_bios.bin and turned "Use a real BIOS" on. This is the
 * same moment for everyone else: the slanted wordmark flies in, a sparkle
 * crosses it and the Nintendo logo follows with the chime. No BIOS, no
 * copyrighted data - every shape and every note is made here.
 */
#ifndef GBAR5_BOOT_INTRO_H
#define GBAR5_BOOT_INTRO_H

#include "../psxs5.h"

/* True when a game should open on the startup animation: "Skip the boot
 * logo" is off, and no real gba_bios.bin is about to draw Nintendo's own. */
bool boot_intro_wanted(const Settings *settings, const char *bios_dir);

/* Starts the animation; the chime begins on the first drawn frame. */
void boot_intro_begin(void);

/* Draws one frame. Returns true when the game should be shown: the animation
 * finished, or Cross / Circle / OPTIONS skipped it. */
bool boot_intro_frame(uint32_t pressed);

#endif
