/*
 * GBAR5 - a neutral startup frame for the public source tree.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The retail Game Boy Advance logo animation is Nintendo's. It is not
 * distributed here, and neither is any other copyrighted boot asset. Supply
 * a real gba_bios.bin (dumped from hardware you own) and turn "Use a real
 * BIOS" on to see the genuine animation, or build an intro from your own
 * footage with tools/make_boot_frames.py (not shipped in this tree).
 *
 * This single white frame keeps the optional built-in intro compiling and
 * silent; it finishes in one frame, so the game starts immediately.
 */
#include "boot_frames.h"

const int boot_frame_w = 240;
const int boot_frame_h = 160;
const int boot_frame_fps = 30;
const unsigned int boot_frame_count = 1;

/* RGBA, one entry. */
const uint8_t boot_frame_palette[4] = {255, 255, 255, 255};

/* Frame 0 is one run of 255 white pixels; decode_frame fills the rest with
 * opaque white, so the whole 240x160 frame is white. */
const uint32_t boot_frame_offset[2] = {0, 2};
const uint8_t boot_frame_data[2] = {255, 0};
