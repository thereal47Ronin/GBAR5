/*
 * GBAR5 - the game's own pictures: an in-game screenshot and the title screen.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Where they come from: libretro-thumbnails' Game Boy Advance set, by name.
 * A GBA cartridge has no disc serial, so the ROM's file name is the key - the
 * same No-Intro name that named the box art - and assets/snaps-index.txt /
 * titles-index.txt (written by tools/make-art-index.py) say which names the
 * set has, so a miss costs one index scan instead of a 404.
 *
 * The shelf draws the selected game's screenshot full screen behind itself and
 * inside the cartridge screen, so a game can be judged before it is started.
 * The picture is cached on the console at <root>/art/<kind>/<id>.png: the
 * second time it is shown it comes off the disk, not the network. One
 * download at a time, on its own thread; nothing blocks the shelf.
 */
#include "art.h"

#include "app.h"
#include "cheats.h"
#include "net.h"
#include "platform/platform.h"
#include "stb_image.h"

#include <SDL2/SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct
{
    const char *folder; /* under <root>/art */
    const char *index;  /* in assets/ */
    const char *base;   /* the download address, before the picture's name */
} KINDS[ART_KIND_COUNT] = {
    /* A GBA has no 4:3 picture in a 16:9 frame; The Bezel Project has no set
     * for it, so this kind stays empty and bezels.c draws nothing. */
    [ART_BEZEL] = {NULL, NULL, NULL},
    [ART_SNAP] = {"snaps", "snaps-index.txt",
                  "https://raw.githubusercontent.com/libretro-thumbnails/"
                  "Nintendo_-_Game_Boy_Advance/master/Named_Snaps/"},
    [ART_TITLE] = {"titles", "titles-index.txt",
                   "https://raw.githubusercontent.com/libretro-thumbnails/"
                   "Nintendo_-_Game_Boy_Advance/master/Named_Titles/"},
};

enum
{
    ST_NEW,     /* not looked at yet */
    ST_LOADED,  /* the texture holds it */
    ST_WAITING, /* a download is wanted (or running) */
    ST_NONE,    /* there is no such picture, or it could not be had this session */
};

#define SLOTS 8

typedef struct
{
    const Game *game;
    int kind, state;
    PlatTexture *texture;
    char name[256]; /* the match in the index */
    uint64_t used;
    unsigned generation; /* for ST_NONE: which download pass last looked */
} Slot;

static Slot slots[SLOTS];
static uint64_t clock_tick;

/* Bumped whenever a download lands, so a game marked "no picture" is looked
 * at once more (a warm-up may have fetched it in the background). */
static SDL_atomic_t art_generation;

static unsigned generation_now(void)
{
    return (unsigned)SDL_AtomicGet(&art_generation);
}

static SDL_atomic_t busy;   /* a download is running */
static SDL_atomic_t result; /* 0 none, 1 arrived, 2 failed: for the slot below */
static int fetch_slot = -1;
static char fetch_url[700], fetch_dest[PSXS5_PATH_MAX];

static void art_path(const Game *g, int kind, char *out, size_t size)
{
    char dir[PSXS5_PATH_MAX], sub[64], file[120];
    snprintf(sub, sizeof(sub), "art/%s", KINDS[kind].folder);
    path_join(dir, sizeof(dir), app.paths.root, sub);
    snprintf(file, sizeof(file), "%.100s.png", g->id);
    path_join(out, size, dir, file);
}

static int fetch_main(void *unused)
{
    (void)unused;
    char temp[PSXS5_PATH_MAX + 8];
    snprintf(temp, sizeof(temp), "%s.part", fetch_dest);
    NetResult r = net_download(fetch_url, temp);
    bool ok = r == NET_OK && rename(temp, fetch_dest) == 0;
    if (!ok)
        remove(temp);
    psxs5_log("art: download %s: %s", fetch_dest, ok ? "ok" : r == NET_NOT_FOUND ? "not found" : "failed");
    if (ok)
        SDL_AtomicAdd(&art_generation, 1);
    SDL_AtomicSet(&result, ok ? 1 : 2);
    SDL_AtomicSet(&busy, 0);
    return 0;
}

static bool fetch_start(int s)
{
    Slot *slot = &slots[s];
    if (!KINDS[slot->kind].base || !KINDS[slot->kind].index)
        return false;
    if (!app.global.cover_download || !net_available() || !SDL_AtomicCAS(&busy, 0, 1))
        return false;
    str_copy(fetch_url, sizeof(fetch_url), KINDS[slot->kind].base);
    size_t w = strlen(fetch_url);
    for (const unsigned char *p = (const unsigned char *)slot->name; *p && w + 4 < sizeof(fetch_url); ++p)
    {
        if (isalnum(*p) || strchr("-._~", *p))
            fetch_url[w++] = (char)*p;
        else
            w += (size_t)snprintf(fetch_url + w, sizeof(fetch_url) - w, "%%%02X", *p);
    }
    fetch_url[w] = '\0';
    art_path(slot->game, slot->kind, fetch_dest, sizeof(fetch_dest));
    char dir[PSXS5_PATH_MAX];
    str_copy(dir, sizeof(dir), fetch_dest);
    char *cut = strrchr(dir, '/');
    if (cut)
        *cut = '\0';
    make_dirs(dir);
    fetch_slot = s;
    SDL_Thread *t = SDL_CreateThread(fetch_main, "art-download", NULL);
    if (t)
    {
        SDL_DetachThread(t);
        return true;
    }
    SDL_AtomicSet(&busy, 0);
    return false;
}

static bool load(Slot *slot)
{
    char path[PSXS5_PATH_MAX];
    art_path(slot->game, slot->kind, path, sizeof(path));
    int w, h, n;
    uint8_t *rgba = stbi_load(path, &w, &h, &n, 4);
    if (!rgba)
        return false;
    slot->texture = plat_texture_create(rgba, w, h, true);
    stbi_image_free(rgba);
    return slot->texture != NULL;
}

static void finished(void)
{
    int r = SDL_AtomicGet(&result);
    if (!r || fetch_slot < 0)
        return;
    SDL_AtomicSet(&result, 0);
    Slot *slot = &slots[fetch_slot];
    fetch_slot = -1;
    if (slot->state != ST_WAITING)
        return; /* the slot went to another picture meanwhile: the file is still there */
    slot->state = r == 1 && load(slot) ? ST_LOADED : ST_NONE;
    slot->generation = generation_now();
}

/* Look the game's name up in the index and, when it is a name the set has,
 * put the download on the one worker. */
static void look_up(Slot *slot)
{
    if (!KINDS[slot->kind].index)
    {
        slot->state = ST_NONE;
        slot->generation = generation_now();
        return;
    }
    if (!app.global.cover_download || !net_available())
    {
        slot->state = ST_NONE; /* still looked at again if downloads come back */
        slot->generation = generation_now();
        return;
    }
    if (cheats_best_in_index(slot->game, KINDS[slot->kind].index, slot->name, sizeof(slot->name)))
    {
        psxs5_log("art: %s %s -> %s", KINDS[slot->kind].folder, slot->game->title, slot->name);
        slot->state = ST_WAITING;
    }
    else
    {
        slot->state = ST_NONE;
    }
    slot->generation = generation_now();
}

PlatTexture *art_get(const Game *g, int kind)
{
    if (!g || kind < 0 || kind >= ART_KIND_COUNT || !KINDS[kind].folder)
        return NULL;
    finished();
    ++clock_tick;
    int s = -1, oldest = 0;
    for (int i = 0; i < SLOTS; ++i)
    {
        if (slots[i].game == g && slots[i].kind == kind)
            s = i;
        if (slots[i].used < slots[oldest].used)
            oldest = i;
    }
    if (s < 0)
    {
        /* reuse the least recently shown slot (never the one downloading) */
        s = oldest == fetch_slot ? (oldest + 1) % SLOTS : oldest;
        Slot *slot = &slots[s];
        plat_texture_free(slot->texture);
        memset(slot, 0, sizeof(*slot));
        slot->game = g;
        slot->kind = kind;
    }
    Slot *slot = &slots[s];
    slot->used = clock_tick;
    if (slot->state == ST_NEW)
    {
        if (load(slot))
            slot->state = ST_LOADED;
        else
            look_up(slot);
    }
    else if (slot->state == ST_NONE && slot->generation != generation_now())
    {
        /* a download finished (or downloads were switched on) since this was
         * looked at: try the disk, then the index, once more */
        if (!load(slot))
            look_up(slot);
        else
            slot->state = ST_LOADED;
    }
    if (slot->state == ST_WAITING && fetch_slot != s)
        fetch_start(s);
    return slot->state == ST_LOADED ? slot->texture : NULL;
}

bool art_pending(const Game *g, int kind)
{
    for (int i = 0; i < SLOTS; ++i)
        if (slots[i].game == g && slots[i].kind == kind)
            return slots[i].state == ST_WAITING;
    return false;
}

void art_warm(const Game *g, int kind)
{
    /* The same lookup as art_get: the picture is fetched and decoded now, so
     * the selection lands on a ready texture instead of an empty screen. */
    (void)art_get(g, kind);
}

void art_refresh(void)
{
    /* Downloads were switched on or off: every "no picture" is looked at
     * again instead of staying that way for the rest of the session. */
    SDL_AtomicAdd(&art_generation, 1);
}

/* ---------------------------------------------------------------- the backdrop */

/* The shelf wants the picture large and quiet behind the interface. Scaling and
 * blending a texture across 1920x1080 every frame costs ~25 ms on the console,
 * so the dimming is baked into the small source picture once per game and the
 * shelf stretches that with blending off. Two slots: the selection and the one
 * it is crossfading away from. */
#define WASH_SLOTS 4

static struct
{
    const Game *game;
    PlatTexture *texture;
    uint8_t keep;
    uint32_t toward;
    int w, h;
    uint64_t used;
} wash[WASH_SLOTS];
static uint64_t wash_tick;

/* The picture at screen size, dimmed by `keep` and a vignette, in one bilinear
 * pass: after this the shelf draws it 1:1 with blending off, which is a copy
 * instead of a scaled alpha blit. The taps are precomputed per column/row so
 * the inner loop is integer work. */
static void wash_scale(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh, uint8_t keep,
                       uint32_t toward)
{
    int *col0 = malloc(sizeof(int) * (size_t)dw);
    int *col1 = malloc(sizeof(int) * (size_t)dw);
    int *colw = malloc(sizeof(int) * (size_t)dw);
    if (!col0 || !col1 || !colw)
    {
        free(col0);
        free(col1);
        free(colw);
        return;
    }
    for (int x = 0; x < dw; ++x)
    {
        int64_t sx = (((int64_t)(2 * x + 1) * sw) << 8) / (2 * dw) - 128;
        if (sx < 0)
            sx = 0;
        int x0 = (int)(sx >> 8);
        if (x0 >= sw)
            x0 = sw - 1;
        col0[x] = x0 * 4;
        col1[x] = (x0 + 1 < sw ? x0 + 1 : x0) * 4;
        colw[x] = (int)(sx & 255);
    }
    const int tr = (int)((toward >> 16) & 0xff), tg = (int)((toward >> 8) & 0xff), tb = (int)(toward & 0xff);
    const int band = dh / 6 > 0 ? dh / 6 : 1;
    const int edge = dw / 8 > 0 ? dw / 8 : 1;
    for (int y = 0; y < dh; ++y)
    {
        int64_t sy = (((int64_t)(2 * y + 1) * sh) << 8) / (2 * dh) - 128;
        if (sy < 0)
            sy = 0;
        int y0 = (int)(sy >> 8);
        if (y0 >= sh)
            y0 = sh - 1;
        int y1 = y0 + 1 < sh ? y0 + 1 : y0;
        int ay = (int)(sy & 255);
        int frow = 256;
        if (y < band)
            frow = 200 + 56 * y / band;
        else if (y >= dh - band)
            frow = 200 + 56 * (dh - 1 - y) / band;
        const uint8_t *r0 = src + (size_t)y0 * (size_t)sw * 4;
        const uint8_t *r1 = src + (size_t)y1 * (size_t)sw * 4;
        uint8_t *out = dst + (size_t)y * (size_t)dw * 4;
        for (int x = 0; x < dw; ++x)
        {
            int fcol = 256;
            if (x < edge)
                fcol = 208 + 48 * x / edge;
            else if (x >= dw - edge)
                fcol = 208 + 48 * (dw - 1 - x) / edge;
            int k = keep * frow * fcol / 65536;
            int inv = 255 - k;
            int a = colw[x], b = 256 - a;
            const uint8_t *p00 = r0 + col0[x], *p10 = r0 + col1[x];
            const uint8_t *p01 = r1 + col0[x], *p11 = r1 + col1[x];
            for (int c = 0; c < 3; ++c)
            {
                int top = (p00[c] * b + p10[c] * a) >> 8;
                int bottom = (p01[c] * b + p11[c] * a) >> 8;
                int v = (top * (256 - ay) + bottom * ay) >> 8;
                int t = c == 0 ? tr : c == 1 ? tg : tb;
                out[x * 4 + c] = (uint8_t)((v * k + t * inv + 128) >> 8);
            }
            out[x * 4 + 3] = 255;
        }
    }
    free(col0);
    free(col1);
    free(colw);
}

PlatTexture *art_wash(const Game *g, uint8_t keep, uint32_t toward)
{
    if (!g)
        return NULL;
    ++wash_tick;
    int scr_w = plat_width(), scr_h = plat_height();
    int s = -1, oldest = 0;
    for (int i = 0; i < WASH_SLOTS; ++i)
    {
        if (wash[i].game == g && wash[i].keep == keep && wash[i].toward == toward && wash[i].w == scr_w &&
            wash[i].h == scr_h)
            s = i;
        if (wash[i].used < wash[oldest].used)
            oldest = i;
    }
    if (s < 0)
    {
        s = oldest;
        plat_texture_free(wash[s].texture);
        memset(&wash[s], 0, sizeof(wash[s]));
        wash[s].game = g;
        wash[s].keep = keep;
        wash[s].toward = toward;

        char path[PSXS5_PATH_MAX];
        int w = 0, h = 0, n = 0;
        art_path(g, ART_SNAP, path, sizeof(path));
        uint8_t *px = stbi_load(path, &w, &h, &n, 4);
        if (!px)
        {
            art_path(g, ART_TITLE, path, sizeof(path));
            px = stbi_load(path, &w, &h, &n, 4);
        }
        if (px)
        {
            int dw = plat_width(), dh = plat_height();
            if (dw > 0 && dh > 0)
            {
                uint8_t *scaled = malloc((size_t)dw * (size_t)dh * 4);
                if (scaled)
                {
                    wash_scale(px, w, h, scaled, dw, dh, keep, toward);
                    wash[s].texture = plat_texture_create(scaled, dw, dh, false);
                    wash[s].w = dw;
                    wash[s].h = dh;
                    free(scaled);
                    psxs5_log("art: backdrop %s (%dx%d from %dx%d)", g->id, dw, dh, w, h);
                }
            }
            stbi_image_free(px);
        }
    }
    wash[s].used = wash_tick;
    return wash[s].texture;
}
