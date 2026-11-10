/*
 * GBAR5 - the optional built-in startup chime.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The public source tree ships no audio: the retail Game Boy Advance startup
 * chime is Nintendo's. boot_chime_frames is 0, so boot_intro.c plays nothing.
 */
#ifndef GBAR5_BOOT_CHIME_H
#define GBAR5_BOOT_CHIME_H

#include <stdint.h>

extern const int boot_chime_rate;
extern const unsigned int boot_chime_frames;
extern const int16_t boot_chime_pcm[];

#endif
