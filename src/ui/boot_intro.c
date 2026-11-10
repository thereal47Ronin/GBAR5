/*
 * GBAR5 - the optional built-in startup.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * With a real gba_bios.bin and "Use a real BIOS" on, mGBA draws Nintendo's
 * own boot sequence and this intro stays out of the way.
 *
 * The public source tree ships a neutral, silent placeholder (boot_frames.c
 * and boot_chime.c) rather than Nintendo's animation and chime. The frame
 * format is 240x160 (the GBA's own screen) at 30 fps; tools/make_boot_frames.py
 * can build a real intro locally from footage you have the rights to.
 */
#include "boot_intro.h"

#include "boot_chime.h"
#include "boot_frames.h"

#include "../app.h"
#include "../core/host.h"
#include "../platform/platform.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>

static bool active;
static bool chime_pending;
static float intro_t;
static PlatTexture *frame_tex;
static uint8_t *frame_pixels; /* w*h RGBA, decoded once per shown frame */

/* A real gba_bios.bin is about to show Nintendo's own animation: don't play
 * ours on top of it. mGBA opens that exact name in the system folder. */
static bool has_gba_bios(const char *dir)
{
    if (!dir || !*dir)
        return false;
    DIR *d = opendir(dir);
    if (!d)
        return false;
    bool found = false;
    struct dirent *e;
    while (!found && (e = readdir(d)))
        found = strcmp(e->d_name, "gba_bios.bin") == 0;
    closedir(d);
    return found;
}

bool boot_intro_wanted(const Settings *settings, const char *bios_dir)
{
    if (!settings || settings->skip_bios)
        return false; /* "Skip the boot logo" is on */
    if (settings->use_bios && has_gba_bios(bios_dir))
        return false; /* the core will draw the real one */
    return true;
}

/* ---------------------------------------------------------------- the frames */

/* One frame of (count, palette index) pairs into w*h RGBA pixels. */
static void decode_frame(unsigned index, uint8_t *dst)
{
    if (index >= boot_frame_count)
        index = boot_frame_count - 1;
    const uint8_t *p = boot_frame_data + boot_frame_offset[index];
    const uint8_t *end = boot_frame_data + boot_frame_offset[index + 1];
    int left = boot_frame_w * boot_frame_h;
    uint32_t *out = (uint32_t *)dst;
    while (p + 1 < end && left > 0)
    {
        int count = p[0];
        const uint8_t *c = boot_frame_palette + (size_t)p[1] * 4;
        uint32_t px = (uint32_t)c[0] | ((uint32_t)c[1] << 8) | ((uint32_t)c[2] << 16) |
                      ((uint32_t)c[3] << 24);
        p += 2;
        if (count > left)
            count = left;
        for (int i = 0; i < count; ++i)
            *out++ = px;
        left -= count;
    }
    if (left > 0) /* a truncated frame: finish it in the paper colour */
        for (int i = 0; i < left; ++i)
            *out++ = 0xffffffffu;
}

/* The clip fills the screen: scaled up as far as it goes with its own shape
 * kept, and the top and bottom of the paper cropped (the logo never goes
 * near either edge). */
static void draw_frame(void)
{
    float sw = (float)plat_width(), sh = (float)plat_height();
    float k = sw / (float)boot_frame_w;
    if (sh / (float)boot_frame_h > k)
        k = sh / (float)boot_frame_h;
    float dw = (float)boot_frame_w * k, dh = (float)boot_frame_h * k;
    plat_draw_texture(frame_tex, (sw - dw) * 0.5f, (sh - dh) * 0.5f, dw, dh, 0xffffffffu, false);
}

/* ---------------------------------------------------------------- the chime */

/* The clip's own chime, resampled to whatever rate the core opened.
 * boot_chime_pcm is mono 16-bit at boot_chime_rate. */
static void play_chime(int rate)
{
    if (rate < 8000)
        rate = 44100;
    if (boot_chime_frames == 0)
        return;
    float step = (float)boot_chime_rate / (float)rate;
    size_t frames = (size_t)((float)boot_chime_frames / step) + 1;
    int16_t *buf = malloc(frames * 2 * sizeof(int16_t));
    if (!buf)
        return;
    const size_t last = boot_chime_frames - 1;
    float pos = 0.0f;
    for (size_t i = 0; i < frames; ++i)
    {
        size_t i0 = (size_t)pos;
        if (i0 > last)
            i0 = last;
        size_t i1 = i0 < last ? i0 + 1 : last;
        float f = i1 == i0 ? 0.0f : pos - (float)i0;
        float v = (float)boot_chime_pcm[i0] +
                  ((float)boot_chime_pcm[i1] - (float)boot_chime_pcm[i0]) * f;
        int s = (int)(v < 0.0f ? v - 0.5f : v + 0.5f);
        buf[i * 2] = (int16_t)s;
        buf[i * 2 + 1] = (int16_t)s;
        pos += step;
    }
    /* the clip already decays into silence; this just kills any end click */
    size_t fade = (size_t)((float)rate * 0.03f);
    for (size_t i = frames > fade ? frames - fade : 0; i < frames; ++i)
    {
        float k = (float)(frames - i) / (float)fade;
        buf[i * 2] = (int16_t)((float)buf[i * 2] * k);
        buf[i * 2 + 1] = (int16_t)((float)buf[i * 2 + 1] * k);
    }
    plat_audio_push(buf, frames);
    psxs5_log("boot intro: chime %u frames @ %d Hz (clip %d Hz, %u samples)", (unsigned)frames,
              rate, boot_chime_rate, boot_chime_frames);
    free(buf);
}

/* ---------------------------------------------------------------- the screen */

void boot_intro_begin(void)
{
    active = true;
    chime_pending = true;
    intro_t = 0.0f;
    if (!frame_tex)
    {
        if (!frame_pixels)
            frame_pixels = malloc((size_t)boot_frame_w * boot_frame_h * 4);
        if (!frame_pixels)
        {
            psxs5_log("boot intro: no memory for the frames");
            active = false;
            return;
        }
        decode_frame(0, frame_pixels);
        frame_tex = plat_texture_create(frame_pixels, boot_frame_w, boot_frame_h, true);
        if (!frame_tex)
        {
            psxs5_log("boot intro: texture failed, skipping the startup");
            active = false;
            return;
        }
    }
    psxs5_log("boot intro: Game Boy Advance startup, %u frames @ %d fps", boot_frame_count,
              boot_frame_fps);
}

bool boot_intro_frame(uint32_t pressed)
{
    if (!active)
        return true;
    if (chime_pending)
    {
        chime_pending = false;
        play_chime(host_sample_rate());
    }
    intro_t += app.dt;

    if (pressed & (BIT(BTN_CROSS) | BIT(BTN_CIRCLE) | BIT(BTN_START)))
    {
        active = false;
        plat_audio_clear();
        return true;
    }

    unsigned index = (unsigned)(intro_t * (float)boot_frame_fps);
    if (index >= boot_frame_count)
    {
        active = false;
        return true;
    }
    decode_frame(index, frame_pixels);
    plat_texture_update(frame_tex, frame_pixels);
    draw_frame();
    return false;
}
