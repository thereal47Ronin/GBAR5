/*
 * GBAR5 - Find a code: a cheat search over the Game Boy Advance's work RAM.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Start a search (a snapshot), play until the value changes, come back and
 * say how it changed; the candidates shrink each round. When few are left,
 * pick one and a value: it becomes a CodeBreaker code in the game's own list
 * (cheats/mine/<game id>.txt) - 3AAAAAAA 00VV for a byte, 8AAAAAAA VVVV for
 * two - addressed from 0x02000000, where EWRAM starts in a cartridge's map.
 */
#include "../app.h"
#include "../cheats.h"
#include "../core/host.h"
#include "../i18n.h"
#include "../platform/platform.h"
#include "deck.h"
#include "draw.h"
#include "icons.h"
#include "libretro.h"
#include "sfx.h"
#include "text.h"
#include "theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the 256 KB of work RAM an emulated cartridge sees at 0x02000000 */
#define RAM_SIZE (256 * 1024)
#define RAM_BASE 0x02000000u
#define SHOW_MAX 40

enum Filter
{
    F_INCREASED,
    F_DECREASED,
    F_UNCHANGED,
    F_CHANGED,
    F_EQUALS,
    F_COUNT
};

static struct
{
    const Game *game;     /* the search belongs to this game */
    bool running;
    int size;             /* 1 or 2 bytes */
    uint8_t *prev;        /* RAM at the last step */
    uint8_t *alive;       /* one bit per address */
    int count;
    int cursor;           /* filter row, or result row */
    int equals;           /* the value for "Equals" */
    int lock;             /* the value a picked address is locked to */
    int results[SHOW_MAX], result_count;
    bool picking;         /* choosing the value for results[cursor] */
} K;

static unsigned value_at(const uint8_t *ram, int a)
{
    return K.size == 2 ? (unsigned)(ram[a] | ram[a + 1] << 8) : ram[a];
}

static bool alive(int a)
{
    return K.alive[a >> 3] & (1u << (a & 7));
}

static void collect(void)
{
    K.result_count = 0;
    const uint8_t *ram = host_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    for (int a = 0; ram && a < RAM_SIZE && K.result_count < SHOW_MAX; a += K.size)
        if (alive(a))
            K.results[K.result_count++] = a;
}

static void start(int size)
{
    const uint8_t *ram = host_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    if (!ram || host_memory_size(RETRO_MEMORY_SYSTEM_RAM) < RAM_SIZE)
    {
        app_toast("This game's memory can't be searched");
        return;
    }
    if (!K.prev)
        K.prev = malloc(RAM_SIZE);
    if (!K.alive)
        K.alive = malloc(RAM_SIZE / 8);
    if (!K.prev || !K.alive)
        return;
    memcpy(K.prev, ram, RAM_SIZE);
    memset(K.alive, 0, RAM_SIZE / 8);
    K.count = 0;
    for (int a = 0; a < RAM_SIZE; a += size)
    {
        K.alive[a >> 3] |= (uint8_t)(1u << (a & 7));
        ++K.count;
    }
    K.size = size;
    K.running = true;
    K.game = app.game;
    K.cursor = 0;
    K.picking = false;
    collect();
}

static void narrow(int filter)
{
    const uint8_t *ram = host_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    if (!ram)
        return;
    int left = 0;
    for (int a = 0; a < RAM_SIZE; a += K.size)
    {
        if (!alive(a))
            continue;
        unsigned now = value_at(ram, a), before = value_at(K.prev, a);
        bool keep = filter == F_INCREASED   ? now > before
                    : filter == F_DECREASED ? now < before
                    : filter == F_UNCHANGED ? now == before
                    : filter == F_CHANGED   ? now != before
                                            : now == (unsigned)K.equals;
        if (keep)
            ++left;
        else
            K.alive[a >> 3] &= (uint8_t)~(1u << (a & 7));
    }
    memcpy(K.prev, ram, RAM_SIZE);
    K.count = left;
    K.cursor = 0;
    collect();
    char msg[64];
    snprintf(msg, sizeof(msg), tr(left == 1 ? "%d address left" : "%d addresses left"), left);
    app_toast(msg);
}

static void save_code(int address)
{
    char code[32], desc[80];
    unsigned v = (unsigned)K.lock & (K.size == 2 ? 0xffffu : 0xffu);
    unsigned at = RAM_BASE + (unsigned)address;
    if (K.size == 2)
        snprintf(code, sizeof(code), "8%07X %04X", at, v);
    else
        snprintf(code, sizeof(code), "3%07X 00%02X", at, v);
    snprintf(desc, sizeof(desc), tr("My code: %07X = %u"), at, v);
    if (cheats_add_user(&app.cheats, app.game, app.paths.cheats, desc, code))
    {
        cheats_apply(&app.cheats);
        app_toast("Saved and switched on: see Cheats");
        sfx_play(SFX_SELECT);
    }
    else
        app_toast("Could not save the code");
}

void cheat_search_open(void)
{
    if (K.game != app.game)
        K.running = false;
    K.cursor = 0;
    K.picking = false;
    if (K.running)
        collect();
    app.screen = SCREEN_CHEAT_SEARCH;
}

void cheat_search_screen(uint32_t pressed)
{
    const uint8_t *ram = host_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    bool results = K.running && K.count <= SHOW_MAX && K.count > 0;
    int rows = !K.running ? 2 : results ? K.result_count + F_COUNT : F_COUNT;
    if (pressed & BIT(BTN_CIRCLE))
    {
        sfx_play(SFX_BACK);
        if (K.picking)
            K.picking = false;
        else
            app.screen = SCREEN_CHEATS;
        return;
    }
    if (K.picking)
    {
        int step = (pressed & (BIT(BTN_L1) | BIT(BTN_R1))) ? 10 : 1;
        if (pressed & (BIT(BTN_RIGHT) | BIT(BTN_R1)))
            K.lock += step;
        if (pressed & (BIT(BTN_LEFT) | BIT(BTN_L1)))
            K.lock -= step;
        int max = K.size == 2 ? 65535 : 255;
        K.lock = K.lock < 0 ? 0 : K.lock > max ? max : K.lock;
        if (pressed & BIT(BTN_CROSS))
        {
            save_code(K.results[K.cursor - F_COUNT]);
            K.picking = false;
        }
    }
    else
    {
        int before = K.cursor;
        if (pressed & BIT(BTN_UP))
            K.cursor = (K.cursor + rows - 1) % rows;
        if (pressed & BIT(BTN_DOWN))
            K.cursor = (K.cursor + 1) % rows;
        if (K.cursor != before)
            sfx_play(SFX_CLICK);
        if (K.running && K.cursor == F_EQUALS && (pressed & (BIT(BTN_LEFT) | BIT(BTN_RIGHT) | BIT(BTN_L1) | BIT(BTN_R1))))
        {
            int step = (pressed & (BIT(BTN_L1) | BIT(BTN_R1))) ? 10 : 1;
            K.equals += (pressed & (BIT(BTN_RIGHT) | BIT(BTN_R1))) ? step : -step;
            int max = K.size == 2 ? 65535 : 255;
            K.equals = K.equals < 0 ? 0 : K.equals > max ? max : K.equals;
        }
        if (pressed & BIT(BTN_CROSS))
        {
            sfx_play(SFX_SELECT);
            if (!K.running)
                start(K.cursor == 0 ? 1 : 2);
            else if (K.cursor < F_COUNT)
                narrow(K.cursor);
            else if (ram)
            {
                K.lock = (int)value_at(ram, K.results[K.cursor - F_COUNT]);
                K.picking = true;
            }
        }
        if ((pressed & BIT(BTN_SQUARE)) && K.running)
        {
            K.running = false;
            K.cursor = 0;
            app_toast("Search cleared");
        }
        if ((pressed & BIT(BTN_TRIANGLE)) && K.running)
        {
            sfx_play(SFX_BACK);
            app.screen = SCREEN_GAME; /* play until the value changes */
            return;
        }
    }

    app_draw_game(40);
    draw_rect(0, 0, plat_width(), plat_height(), TH_SCRIM);
    deck_draw(1.0f);
    deck_draw_header(1.0f);

    const float x = 112, y = deck_top(), w = plat_width() - 224,
                h = deck_bottom() - deck_top();
    draw_rrect(x, y, w, h, 22, argb_alpha(TH_CARD, 0.78f));
    draw_rrect_outline(x, y, w, h, 22, 1.0f, TH_DIVIDER);
    text_draw(x + 36, y + 26, 26, FONT_BOLD, TH_TEXT, ALIGN_LEFT, tr("Find a code"));
    char sub[200], meta[64] = "";
    if (!K.running)
        str_copy(sub, sizeof(sub), tr("Start, play until the value changes (lives, money...), come back and say how it changed."));
    else
        snprintf(sub, sizeof(sub), tr(K.count == 1 ? "%d address left: play, then say how the value changed."
                                                   : "%d addresses left: play, then say how the value changed."),
                 K.count);
    if (K.running)
        snprintf(meta, sizeof(meta), "%d", K.count);
    if (meta[0])
        text_draw(x + w - 36, y + 32, 17, FONT_MONO, TH_HINT, ALIGN_RIGHT, meta);
    draw_rect(x + 36, y + 78, w - 72, 1, TH_DIVIDER);

    const float ix = x + 36, iw = w - 72, sub_y = y + 100;
    text_draw_fit(ix, sub_y, 18, FONT_REGULAR, TH_TEXT_DIM, ALIGN_LEFT, iw, sub);

    const float top = sub_y + 50, row_h = 66;
    static const char *const starts[] = {"New search: 1 byte (0 to 255)", "New search: 2 bytes (0 to 65535)"};
    static const char *const filters[F_COUNT] = {"Increased", "Decreased", "Unchanged", "Changed", "Equals"};
    int visible = (int)((deck_bottom() - 30 - top) / row_h);
    if (visible < 1)
        visible = 1;
    int first = K.cursor >= visible ? K.cursor - visible + 1 : 0;
    for (int i = first; i < rows && i < first + visible; ++i)
    {
        float ry = top + (float)(i - first) * row_h;
        bool on = i == K.cursor;
        char label[96], value[64] = "";
        if (!K.running)
            str_copy(label, sizeof(label), tr(starts[i]));
        else if (i < F_COUNT)
        {
            str_copy(label, sizeof(label), tr(filters[i]));
            if (i == F_EQUALS)
                snprintf(value, sizeof(value), "< %d >", K.equals);
        }
        else
        {
            int a = K.results[i - F_COUNT];
            snprintf(label, sizeof(label), "%06X", (unsigned)a);
            if (on && K.picking)
                snprintf(value, sizeof(value), tr("lock to < %d >"), K.lock);
            else if (ram)
                snprintf(value, sizeof(value), tr("now %u"), value_at(ram, a));
        }
        if (on)
        {
            draw_glow(ix - 6, ry - 6, iw + 12, row_h + 12, 12, argb_alpha(TH_FOCUS, 0.08f));
            draw_rrect(ix, ry, iw, row_h - 8, 12, TH_ROW_SELECTED);
            draw_rrect(ix + 12, ry + row_h * 0.5f - 12, 4, 24, 2, TH_FOCUS);
            draw_rrect_outline(ix, ry, iw, row_h - 8, 12, 2.0f, argb_alpha(TH_FOCUS, 0.65f));
        }
        text_draw(ix + 30, ry + 15, 23, on ? FONT_BOLD : FONT_REGULAR,
                  on ? TH_TEXT : TH_TEXT_SOFT, ALIGN_LEFT, label);
        if (value[0])
            text_draw(ix + iw - 26, ry + 15, 23, FONT_REGULAR, TH_FOCUS, ALIGN_RIGHT, value);
        if (K.running && i == F_COUNT - 1 && results)
            draw_rect(ix + 20, ry + row_h - 8, iw - 40, 1, TH_DIVIDER);
    }

    if (K.picking)
    {
        static const int g[] = {GLYPH_LEFT, GLYPH_RIGHT, GLYPH_CROSS, GLYPH_CIRCLE};
        static const char *const l[] = {"-1", "+1", "Save the code", "Back"};
        app_draw_hints(g, l, 4, "L1 / R1  -10 / +10");
    }
    else if (K.running)
    {
        static const int g[] = {GLYPH_CROSS, GLYPH_TRIANGLE, GLYPH_SQUARE, GLYPH_CIRCLE};
        static const char *const l[] = {"Choose", "Play", "Clear", "Back"};
        app_draw_hints(g, l, 4, NULL);
    }
    else
    {
        static const int g[] = {GLYPH_CROSS, GLYPH_CIRCLE};
        static const char *const l[] = {"Start", "Back"};
        app_draw_hints(g, l, 2, NULL);
    }
    app_draw_toast();
}
