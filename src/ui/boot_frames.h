/*
 * GBAR5 - the optional built-in startup.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The public source tree ships a neutral placeholder frame (boot_frames.c) and
 * no chime: the retail Game Boy Advance animation and sound are Nintendo's and
 * are not distributed here. With a real gba_bios.bin and "Use a real BIOS" on,
 * mGBA draws the genuine boot sequence and this intro stays out of the way.
 */
#ifndef GBAR5_BOOT_FRAMES_H
#define GBAR5_BOOT_FRAMES_H

#include <stdint.h>

extern const int boot_frame_w;
extern const int boot_frame_h;
extern const int boot_frame_fps;
extern const unsigned int boot_frame_count;

/* RGBA bytes, 4 per palette entry. */
extern const uint8_t boot_frame_palette[];
/* boot_frame_offset[n] .. boot_frame_offset[n+1] is frame n, as (count, index)
 * pairs covering w*h pixels. */
extern const uint32_t boot_frame_offset[];
extern const uint8_t boot_frame_data[];

#endif
