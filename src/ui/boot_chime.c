/*
 * GBAR5 - silent placeholder for the optional built-in startup chime.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The Game Boy Advance startup chime is Nintendo's. It is not distributed in
 * this source tree. boot_intro.c plays nothing when boot_chime_frames is 0,
 * so the app still boots normally.
 */
#include "boot_chime.h"

const int boot_chime_rate = 44100;
const unsigned int boot_chime_frames = 0;
const int16_t boot_chime_pcm[1] = {0};
