/*
 * PSXS5 - the in-game menu (a side panel over the paused game) and cheats.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "../app.h"
#include "../core/host.h"
#include "../covers.h"
#include "../i18n.h"
#include "../net.h"
#include "../play.h"
#include "../platform/platform.h"
#include "../ra/achievements.h"
#include "coverflow.h"
#include "deck.h"
#include "draw.h"
#include "icons.h"
#include "sfx.h"
#include "text.h"
#include "theme.h"

#include "stb_image.h"

#include <SDL2/SDL.h>

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

enum Item
{
    MI_RESUME,
    MI_SAVE,
    MI_LOAD,
    MI_AUTO,
    MI_DISC,
    MI_CHEATS,
    MI_ACHIEVEMENTS,
    MI_MANUAL,
    MI_GUIDE,
    MI_SETTINGS,
    MI_RESET,
    MI_QUIT,
    MI_COUNT
};

/* slot thumbnails, loaded when the menu opens */
static PlatTexture *thumbs[10];
static bool thumbs_loaded[10];

static void forget_thumbs(void)
{
    for (int s = 0; s < 10; ++s)
    {
        plat_texture_free(thumbs[s]);
        thumbs[s] = NULL;
        thumbs_loaded[s] = false;
    }
}

/* Which emulator made a save state, from the signature each writes first */
static bool state_core_read(const char *state, char *out, size_t size)
{
    char head[16] = {0};
    FILE *f = fopen(state, "rb");
    if (!f)
        return false;
    size_t n = fread(head, 1, sizeof(head), f);
    fclose(f);
    const char *name = n >= 8 && !memcmp(head, "MDFNSVST", 8)   ? "Beetle PSX HW"
                       : n >= 4 && !memcmp(head, "DUCC", 4)      ? "SwanStation"
                       : n >= 9 && !memcmp(head, "STv4 PCSX", 9) ? "PCSX-ReARMed"
                                                                 : NULL;
    if (name)
        str_copy(out, size, name);
    return name != NULL;
}

static PlatTexture *slot_thumb(int slot)
{
    if (!thumbs_loaded[slot])
    {
        thumbs_loaded[slot] = true;
        static uint8_t rgba[THUMB_W * THUMB_H * 4];
        char path[PSXS5_PATH_MAX];
        app_state_path(path, sizeof(path), slot);
        if (play_load_thumb(path, rgba))
            thumbs[slot] = plat_texture_create(rgba, THUMB_W, THUMB_H, true);
    }
    return thumbs[slot];
}

static struct
{
    int cursor;
    float sel_y, open_t, slot_x, pane_t;
    int disc_choice;
    int auto_choice; /* which auto-save, newest first */
    int cheat_cursor;
    float cheat_y, cheat_scroll;
} M;

void menu_open(void)
{
    M.cursor = MI_RESUME;
    M.open_t = 0;
    M.sel_y = 0;
    M.pane_t = 0;
    M.disc_choice = host_disc_index();
    M.auto_choice = 0;
    forget_thumbs();
    play_save_resume(true); /* the menu is a good moment: nothing is moving */
    app.screen = SCREEN_MENU;
    sfx_play(SFX_SELECT);
}

static bool item_shown(int i)
{
    int unlocked, total;
    if (i == MI_MANUAL)
        return manual_page_count() > 0;
    if (i == MI_GUIDE)
        return guide_count() > 0;
    if (i == MI_AUTO)
    {
        int slots[AUTO_SLOTS];
        long ages[AUTO_SLOTS];
        return play_auto_list(slots, ages) > 0;
    }
    if (i == MI_ACHIEVEMENTS) /* GBAR5: RetroAchievements' disc hash was PS1's */
        return false;
    if (i == MI_ACHIEVEMENTS)
        return ra_game_progress(&unlocked, &total);
    return i != MI_DISC || host_disc_count() > 1;
}

/* "2 min ago", "Yesterday"...; "" when the slot is empty */
static void slot_age(int slot, char *out, size_t size)
{
    char path[PSXS5_PATH_MAX];
    app_state_path(path, sizeof(path), slot);
    struct stat st;
    out[0] = '\0';
    if (stat(path, &st) != 0)
        return;
    long ago = (long)(time(NULL) - st.st_mtime);
    if (ago < 60)
        str_copy(out, size, tr("Just now"));
    else if (ago < 3600)
        snprintf(out, size, tr("%ld min ago"), ago / 60);
    else if (ago < 86400)
        snprintf(out, size, tr("%ld h ago"), ago / 3600);
    else if (ago < 172800)
        str_copy(out, size, tr("Yesterday"));
    else
        snprintf(out, size, tr("%ld days ago"), ago / 86400);
}

/* The note written on a slot from the phone (<state>.note), re-read every
 * two seconds so a note typed while the menu is open shows up. */
static const char *slot_note(int slot)
{
    static char notes[10][124];
    static uint64_t read_at;
    static const Game *read_for;
    uint64_t now = plat_ticks_us();
    if (read_for != app.game || now - read_at > 2000000)
    {
        read_for = app.game;
        read_at = now;
        for (int s = 0; s < 10; ++s)
        {
            char path[PSXS5_PATH_MAX + 8];
            app_state_path(path, sizeof(path) - 8, s);
            strcat(path, ".note");
            notes[s][0] = '\0';
            FILE *f = fopen(path, "rb");
            if (f)
            {
                size_t n = fread(notes[s], 1, sizeof(notes[s]) - 1, f);
                notes[s][n] = '\0';
                fclose(f);
            }
        }
    }
    return notes[slot];
}

/* ---------------------------------------------------------------- the deck */

/* The in-game overlay is a deck: one card over the dimmed game, a wordmark and
 * the game's identity in its head, the actions down the left, and a detail pane
 * on the right that shows the thing the chosen row would open. Not a bare list
 * of quick-menu rows: every row has its context on screen before you press. */

static float deck_list_w(void) { return 452.0f; }
float deck_top(void) { return 264.0f; }
float deck_bottom(void) { return plat_height() - 146.0f; }
static float deck_pane_x(void) { return 112.0f + deck_list_w() + 32.0f; }
static float deck_pane_w(void) { return plat_width() - 112.0f - deck_pane_x(); }

#define PANE_IX (deck_pane_x() + 40.0f)
#define PANE_IY (deck_top() + 104.0f)
#define PANE_IW (deck_pane_w() - 80.0f)
#define PANE_IB (deck_bottom() - 30.0f)

static const struct
{
    const char *name;
    int icon;
    int group;
} ITEMS[MI_COUNT] = {
    {"Resume", ICON_PLAYER_PLAY, 0},
    {"Save state", ICON_DEVICE_FLOPPY, 1},
    {"Load state", ICON_HISTORY, 1},
    {"Auto-saves", ICON_CLOCK, 1},
    {"Disc", ICON_DISC, 2},
    {"Cheats", ICON_CODE, 2},
    {"Achievements", ICON_TROPHY, 2},
    {"Manual", ICON_BOOKS, 2},
    {"Guide", ICON_FILE_TEXT, 2},
    {"Settings", ICON_ADJUSTMENTS, 4},
    {"Reset", ICON_REFRESH, 4},
    {"Quit to game library", ICON_DOOR_EXIT, 4},
};

static const char *const GROUPS[] = {"Play", "Saves", "Tools", "Settings", "System"};

/* A cartridge, drawn small: the app's own mark, not a system menu's icon. */
static void draw_cart(float x, float y, float h, uint32_t body, uint32_t face)
{
    const float w = h * 0.78f;
    draw_rrect(x, y, w, h, h * 0.10f, body);
    draw_rrect(x + w * 0.14f, y + h * 0.10f, w * 0.72f, h * 0.40f, h * 0.05f, face);
    draw_rect(x + w * 0.24f, y + h * 0.61f, w * 0.52f, fmaxf(2.0f, h * 0.045f),
              argb_alpha(face, 0.45f));
    draw_rect(x + w * 0.24f, y + h * 0.73f, w * 0.52f, fmaxf(2.0f, h * 0.045f),
              argb_alpha(face, 0.45f));
}

static void play_time_text(char *out, size_t size)
{
    int minutes = (int)(app.play_seconds / 60.0);
    if (minutes < 1)
        str_copy(out, size, tr("Just started"));
    else if (minutes < 60)
        snprintf(out, size, tr("Playing for %d min"), minutes);
    else
        snprintf(out, size, tr("Playing for %d h %02d"), minutes / 60, minutes % 60);
}

void deck_draw(float a)
{
    const float x = 64.0f, y = 40.0f, w = plat_width() - 128.0f, h = plat_height() - 80.0f;
    draw_glow(x + 10, y + 18, w - 20, h - 16, 30, argb_alpha(0xff000000u, 0.45f * a));
    draw_rrect(x, y, w, h, 28, argb_alpha(TH_BG, 0.95f * a));
    draw_rrect_outline(x, y, w, h, 28, 1.5f, argb_alpha(TH_DIVIDER, a));
    /* a two-tone accent rule over the wordmark: the deck's signature */
    draw_rrect(x + 48, y + 18, 132, 4, 2, argb_alpha(TH_FOCUS, 0.9f * a));
    draw_rrect(x + 186, y + 18, 44, 4, 2, argb_alpha(TH_FOCUS, 0.28f * a));
}

void deck_draw_header(float a)
{
    const float right = plat_width() - 112.0f;

    draw_cart(112, 68, 24, argb_alpha(TH_FOCUS, a), argb_alpha(TH_BG_DEEP, a));
    text_draw(148, 64, 22, FONT_BOLD, argb_alpha(TH_TEXT, a), ALIGN_LEFT, "GBAR5");
    const float wx = 148 + text_width(22, FONT_BOLD, "GBAR5") + 26;
    const char *paused = tr("Paused");
    const float pillw = text_width(15, FONT_BOLD, paused) + 54;
    draw_rrect(wx, 63, pillw, 28, 14, argb_alpha(TH_FOCUS, 0.13f * a));
    draw_rect(wx + 17, 71, 4, 12, argb_alpha(TH_FOCUS, a));
    draw_rect(wx + 25, 71, 4, 12, argb_alpha(TH_FOCUS, a));
    text_draw(wx + 42, 68, 15, FONT_BOLD, argb_alpha(TH_FOCUS, a), ALIGN_LEFT, paused);

    char clock[32], played[96];
    plat_clock(clock, sizeof(clock));
    play_time_text(played, sizeof(played));
    for (int k = 0; k < PSXS5_MAX_PADS; ++k)
    {
        int level = app.pads[k].connected ? plat_pad_battery(k) : -1;
        if (level >= 0)
            snprintf(played + strlen(played), sizeof(played) - strlen(played), "%sP%d %d%%",
                     strchr(played, ' ') ? "  \xc2\xb7  " : "", k + 1, level);
    }
    text_draw(right, 56, 30, FONT_BOLD, argb_alpha(TH_TEXT, a), ALIGN_RIGHT, clock);
    text_draw(right, 96, 16, FONT_MONO, argb_alpha(TH_HINT, a), ALIGN_RIGHT, played);

    /* the game */
    const Game *g = app.game;
    const float cy = 104, ch = 116;
    float cx = 112, cw = ch * 0.78f;
    PlatTexture *cover = g ? covers_get(app.game_index) : NULL;
    if (cover)
    {
        int tw = 1, th = 1;
        plat_texture_size(cover, &tw, &th);
        cw = ch * (float)tw / (float)th;
        plat_draw_texture(cover, cx, cy, cw, ch, argb_alpha(0xffffffffu, a), true);
        draw_rrect_outline(cx, cy, cw, ch, 6, 1, argb_alpha(TH_DIVIDER, a));
    }
    else
    {
        draw_rrect(cx, cy, cw, ch, 8, argb_alpha(TH_CARD, a));
        draw_cart(cx + cw * 0.11f, cy + 16, ch - 32, argb_alpha(TH_TEXT_DIM, a),
                  argb_alpha(TH_CARD, a));
    }
    if (g)
    {
        const float tx = cx + cw + 28;
        text_draw_fit(tx, cy - 4, 36, FONT_BOLD, argb_alpha(TH_TEXT, a), ALIGN_LEFT,
                      right - tx - 260, g->title);
        float ly = cy + 52;
        if (g->serial[0])
        {
            text_draw(tx, ly, 18, FONT_MONO, argb_alpha(TH_TEXT_DIM, a), ALIGN_LEFT, g->serial);
            ly += 32;
        }
        int unlocked, total;
        if (ra_game_progress(&unlocked, &total))
        {
            icon_draw(ICON_TROPHY, tx, ly - 1, 20, argb_alpha(TH_GOLD, a));
            char ach[64];
            snprintf(ach, sizeof(ach), tr("%d of %d achievements"), unlocked, total);
            text_draw(tx + 28, ly - 2, 18, FONT_REGULAR, argb_alpha(TH_TEXT_SOFT, a), ALIGN_LEFT,
                      ach);
            const float barw = 200, bx = tx + 46 + text_width(18, FONT_REGULAR, ach);
            draw_rrect(bx, ly + 7, barw, 6, 3, argb_alpha(TH_PILL, a));
            if (total > 0)
                draw_rrect(bx, ly + 7, barw * (float)unlocked / (float)total, 6, 3,
                           argb_alpha(TH_GOLD, a));
        }
    }

    draw_rect(112, 240, plat_width() - 224, 1, argb_alpha(TH_DIVIDER, a));
    draw_rect(112, 240, 220, 1, argb_alpha(TH_FOCUS, 0.55f * a));
}

/* The right-hand card every section draws into. */
static void draw_pane_frame(float a, const char *title, const char *meta)
{
    const float x = deck_pane_x(), y = deck_top(), w = deck_pane_w(),
                h = deck_bottom() - deck_top();
    draw_rrect(x, y, w, h, 22, argb_alpha(TH_CARD, 0.78f * a));
    draw_rrect_outline(x, y, w, h, 22, 1.0f, argb_alpha(TH_DIVIDER, a));
    text_draw_fit(x + 36, y + 26, 26, FONT_BOLD, argb_alpha(TH_TEXT, a), ALIGN_LEFT, w - 200, title);
    if (meta && meta[0])
        text_draw(x + w - 36, y + 32, 17, FONT_MONO, argb_alpha(TH_HINT, a), ALIGN_RIGHT, meta);
    draw_rect(x + 36, y + 78, w - 72, 1, argb_alpha(TH_DIVIDER, a));
    /* a short accent rule closing the pane's head */
    draw_rrect(x + w - 92, y + 62, 56, 3, 1.5f, argb_alpha(TH_FOCUS, 0.55f * a));
}

static void draw_deck_list(float a, int autos, int discs)
{
    const float x = 112, w = deck_list_w();
    int shown[MI_COUNT], count = 0, groups = 0, last = -1;
    for (int i = 0; i < MI_COUNT; ++i)
        if (item_shown(i))
        {
            shown[count++] = i;
            if (ITEMS[i].group != last)
            {
                ++groups;
                last = ITEMS[i].group;
            }
        }
    if (count == 0)
        return;

    float avail = deck_bottom() - deck_top() - (float)groups * 32.0f;
    float stride = avail / (float)count;
    if (stride > 62.0f)
        stride = 62.0f;
    if (stride < 44.0f)
        stride = 44.0f;
    const float row_h = stride - 6.0f;

    float y = deck_top(), sel_target = deck_top();
    int lastgroup = -1;
    for (int k = 0; k < count; ++k)
    {
        int i = shown[k];
        if (ITEMS[i].group != lastgroup)
        {
            lastgroup = ITEMS[i].group;
            y += 32;
        }
        if (i == M.cursor)
            sel_target = y;
        y += stride;
    }
    if (M.sel_y == 0)
        M.sel_y = sel_target;
    anim_approach(&M.sel_y, sel_target, app.dt, TH_SNAP);

    /* the selected row: a filled card, an accent spine, a ring */
    draw_glow(x - 5, M.sel_y - 5, w + 10, row_h + 10, 10, argb_alpha(TH_FOCUS, 0.07f * a));
    draw_rrect(x, M.sel_y, w, row_h, 14, argb_alpha(TH_ROW_SELECTED, a));
    draw_rrect(x + 12, M.sel_y + row_h * 0.5f - 11, 4, 22, 2, argb_alpha(TH_FOCUS, a));
    draw_rrect_outline(x, M.sel_y, w, row_h, 14, 2.0f, argb_alpha(TH_FOCUS, 0.65f * a));

    lastgroup = -1;
    y = deck_top();
    for (int k = 0; k < count; ++k)
    {
        int i = shown[k];
        if (ITEMS[i].group != lastgroup)
        {
            lastgroup = ITEMS[i].group;
            text_draw(x + 6, y + 8, 13, FONT_MONO, argb_alpha(TH_HINT, 0.80f * a), ALIGN_LEFT,
                      tr(GROUPS[lastgroup]));
            y += 32;
        }
        const float ty = y + (row_h - 24.0f) * 0.5f - 1.0f;
        bool sel = i == M.cursor;
        uint32_t ink = sel ? TH_TEXT : (i == MI_QUIT ? TH_DANGER : TH_TEXT_SOFT);
        icon_draw(ITEMS[i].icon, x + 30, y + (row_h - 24.0f) * 0.5f, 24,
                  sel ? TH_FOCUS : (i == MI_QUIT ? TH_DANGER : TH_TEXT_DIM));
        text_draw(x + 68, ty, 24, sel ? FONT_BOLD : FONT_REGULAR, argb_alpha(ink, a), ALIGN_LEFT,
                  tr(ITEMS[i].name));

        char value[64];
        bool choice = false;
        value[0] = '\0';
        if (i == MI_DISC)
        {
            snprintf(value, sizeof(value), tr("%d of %d"), M.disc_choice + 1, discs);
            choice = true;
        }
        else if (i == MI_CHEATS)
        {
            int on = 0;
            for (int k2 = 0; k2 < app.cheats.count; ++k2)
                on += app.cheats.items[k2].enabled;
            if (ra_hardcore())
                str_copy(value, sizeof(value), tr("hardcore"));
            else if (app.cheats.count)
                snprintf(value, sizeof(value), tr("%d on"), on);
        }
        else if (i == MI_LOAD && ra_hardcore())
            str_copy(value, sizeof(value), tr("hardcore"));
        else if (i == MI_ACHIEVEMENTS)
        {
            int unlocked, total;
            if (ra_game_progress(&unlocked, &total))
                snprintf(value, sizeof(value), "%d / %d", unlocked, total);
        }
        if (value[0])
        {
            if (choice)
                draw_choice(x + w - 18, y + (row_h - 34.0f) * 0.5f, 34, 18, TH_PILL,
                            argb_alpha(TH_TEXT_SOFT, a), value);
            else
                text_draw(x + w - 22, ty + 3, 19, FONT_REGULAR, argb_alpha(TH_TEXT_DIM, a),
                          ALIGN_RIGHT, value);
        }
        y += stride;
    }
    if (y < deck_bottom() - 54.0f)
        text_draw(x + 6, deck_bottom() - 26, 13, FONT_MONO, argb_alpha(TH_HINT, 0.55f * a),
                  ALIGN_LEFT, "GBAR5 " PSXS5_VERSION);
}

/* ---------------------------------------------------------------- the panes */

static void pane_resume(float a)
{
    const Game *g = app.game;
    const float art_h = 328;
    float art_w = art_h * 0.70f, ax = PANE_IX, ay = PANE_IY;
    PlatTexture *cover = g ? covers_get(app.game_index) : NULL;
    if (cover)
    {
        int tw = 1, th = 1;
        plat_texture_size(cover, &tw, &th);
        art_w = art_h * (float)tw / (float)th;
        plat_draw_texture(cover, ax, ay, art_w, art_h, argb_alpha(0xffffffffu, a), true);
        draw_rrect_outline(ax, ay, art_w, art_h, 8, 1.5f, argb_alpha(TH_DIVIDER, a));
    }
    else
    {
        draw_rrect(ax, ay, art_w, art_h, 10, argb_alpha(TH_BG_DEEP, a));
        draw_cart(ax + art_w * 0.18f, ay + art_h * 0.22f, art_h * 0.56f,
                  argb_alpha(TH_TEXT_DIM, a), argb_alpha(TH_BG_DEEP, a));
    }

    const float tx = ax + art_w + 40, tw = PANE_IX + PANE_IW - tx;
    text_draw(tx, ay + 6, 14, FONT_MONO, argb_alpha(TH_FOCUS, a), ALIGN_LEFT,
              tr("Ready when you are"));
    if (g)
        text_draw_fit(tx, ay + 32, 38, FONT_BOLD, argb_alpha(TH_TEXT, a), ALIGN_LEFT, tw, g->title);
    if (g && g->serial[0])
        text_draw(tx, ay + 86, 18, FONT_MONO, argb_alpha(TH_TEXT_DIM, a), ALIGN_LEFT, g->serial);

    int unlocked, total;
    float ly = ay + 132;
    if (ra_game_progress(&unlocked, &total))
    {
        char ach[64];
        snprintf(ach, sizeof(ach), tr("%d of %d achievements"), unlocked, total);
        icon_draw(ICON_TROPHY, tx, ly - 1, 20, argb_alpha(TH_GOLD, a));
        text_draw(tx + 28, ly - 2, 18, FONT_REGULAR, argb_alpha(TH_TEXT_SOFT, a), ALIGN_LEFT, ach);
        const float barw = tw < 380 ? tw : 380;
        draw_rrect(tx, ly + 30, barw, 7, 3.5f, argb_alpha(TH_PILL, a));
        if (total > 0)
            draw_rrect(tx, ly + 30, barw * (float)unlocked / (float)total, 7, 3.5f,
                       argb_alpha(TH_GOLD, a));
        ly += 62;
    }

    /* three facts as pills: time in, the slot that will be used, cheats on */
    char pill[96], played[48];
    float pxx = tx;
    play_time_text(played, sizeof(played));
    pxx += draw_pill(pxx, ly, 40, 18, TH_PILL, argb_alpha(TH_TEXT_SOFT, a), played) + 12;
    snprintf(pill, sizeof(pill), tr("Slot %d"), app.settings.state_slot);
    pxx += draw_pill(pxx, ly, 40, 18, TH_PILL, argb_alpha(TH_TEXT_SOFT, a), pill) + 12;
    int on = 0;
    for (int k = 0; k < app.cheats.count; ++k)
        on += app.cheats.items[k].enabled;
    if (app.cheats.count)
    {
        if (on == 1)
            str_copy(pill, sizeof(pill), tr("1 cheat on"));
        else
            snprintf(pill, sizeof(pill), tr("%d cheats on"), on);
        draw_pill(pxx, ly, 40, 18, TH_PILL, argb_alpha(TH_TEXT_SOFT, a), pill);
    }

    /* the button the row stands for: what Cross will do, before it is pressed */
    const float bw = PANE_IW, bh = 68, by = PANE_IB - bh - 40; /* clear of the toast */
    draw_rrect(PANE_IX, by, bw, bh, 16, argb_alpha(TH_FOCUS, 0.14f * a));
    draw_rrect_outline(PANE_IX, by, bw, bh, 16, 1.5f, argb_alpha(TH_FOCUS, 0.45f * a));
    icon_draw(ICON_PLAYER_PLAY, PANE_IX + 26, by + 21, 26, argb_alpha(TH_FOCUS, a));
    text_draw(PANE_IX + 66, by + 18, 26, FONT_BOLD, argb_alpha(TH_FOCUS, a), ALIGN_LEFT,
              tr("Resume the game"));
    float hx = PANE_IX + bw - 18 - text_width(17, FONT_REGULAR, tr("Cross"));
    text_draw(hx, by + 24, 17, FONT_REGULAR, argb_alpha(TH_HINT, a), ALIGN_LEFT, tr("Cross"));
    draw_pad_glyph(GLYPH_CROSS, hx - 20, by + 34, 20);
}

static void draw_slot_tile(int s, float x, float y, float w, float h, bool on, float a)
{
    char age[48];
    slot_age(s, age, sizeof(age));
    PlatTexture *thumb = age[0] ? slot_thumb(s) : NULL;
    uint32_t fill = argb_alpha(age[0] ? TH_CARD : TH_BG_DEEP, a);
    if (on)
    {
        draw_glow(x - 6, y - 6, w + 12, h + 12, 16, argb_alpha(TH_FOCUS, 0.14f * a));
        fill = argb_lerp(fill, argb_alpha(TH_ROW_SELECTED, a), 0.60f);
        fill = argb_lerp(fill, argb_alpha(TH_FOCUS, 0.20f * a), 0.30f);
    }
    draw_rrect(x, y, w, h, 12, fill);
    const char *note = slot_note(s);
    char name[32];
    snprintf(name, sizeof(name), tr("Slot %d"), s);
    if (thumb)
    {
        plat_draw_texture(thumb, x + 3, y + 3, w - 6, h - 6, argb_alpha(0xffffffffu, a), false);
        draw_rect(x + 3, y + h - 46, w - 6, 43, argb_alpha(TH_BG_DEEP, 0.82f * a));
        text_draw_fit(x + 13, y + h - 42, 17, FONT_BOLD, argb_alpha(TH_TEXT, a), ALIGN_LEFT,
                      w - 22, note[0] ? note : name);
        text_draw_fit(x + 13, y + h - 22, 15, FONT_REGULAR, argb_alpha(TH_TEXT_SOFT, a),
                      ALIGN_LEFT, w - 22, age);
    }
    else
    {
        draw_cart(x + w * 0.5f - w * 0.07f, y + 20, 30, argb_alpha(TH_SWITCH_OFF, a),
                  argb_alpha(TH_BG_DEEP, a));
        text_draw_fit(x + 13, y + h - 62, 19, FONT_BOLD, argb_alpha(TH_TEXT_SOFT, a), ALIGN_LEFT,
                      w - 22, note[0] ? note : name);
        text_draw(x + 13, y + h - 36, 16, FONT_REGULAR, argb_alpha(TH_TEXT_DIM, a), ALIGN_LEFT,
                  tr("Empty"));
    }
    if (on)
    {
        draw_rrect_outline(x, y, w, h, 12, 3.0f, argb_alpha(TH_FOCUS, a));
        draw_rrect(x + w * 0.5f - 14, y + h - 3, 28, 3, 1.5f, argb_alpha(TH_FOCUS, a));
    }
    else
        draw_rrect_outline(x, y, w, h, 12, 1.0f, argb_alpha(TH_DIVIDER, a));
}

static void pane_slots(float a, bool loading)
{
    const int visible = 5;
    const float gap = 16, tw = (PANE_IW - gap * (visible - 1)) / visible, th = tw * 0.70f;
    int slot = app.settings.state_slot;
    for (int s = 0; s < 10; ++s)
    {
        float tx = PANE_IX + (s % visible) * (tw + gap);
        float ty = PANE_IY + (s / visible) * (th + gap);
        draw_slot_tile(s, tx, ty, tw, th, s == slot, a);
    }

    /* the chosen slot called out, with what Cross will do to it */
    char meta[160], age[48], name[40];
    slot_age(slot, age, sizeof(age));
    const char *note = slot_note(slot);
    snprintf(name, sizeof(name), tr("Slot %d"), slot);
    const float gy = PANE_IY + 2 * th + gap + 24, gh = PANE_IB - gy;
    draw_rrect(PANE_IX, gy, PANE_IW, gh, 16, argb_alpha(TH_BG_DEEP, 0.55f * a));
    draw_rrect_outline(PANE_IX, gy, PANE_IW, gh, 16, 1.0f, argb_alpha(TH_DIVIDER, a));
    draw_rrect(PANE_IX + 28, gy + 30, 4, gh - 60, 2, argb_alpha(TH_FOCUS, a));
    text_draw(PANE_IX + 48, gy + 24, 13, FONT_MONO, argb_alpha(TH_HINT, a), ALIGN_LEFT,
              tr("Selected slot"));
    text_draw(PANE_IX + 48, gy + 46, 30, FONT_BOLD, argb_alpha(TH_TEXT, a), ALIGN_LEFT, name);
    if (note[0])
        snprintf(meta, sizeof(meta), "%s  \xc2\xb7  %s", note, age[0] ? age : tr("Empty"));
    else
        snprintf(meta, sizeof(meta), "%s", age[0] ? age : tr("Empty"));
    text_draw_fit(PANE_IX + 48 + text_width(30, FONT_BOLD, name) + 22, gy + 54, 18,
                  FONT_REGULAR, argb_alpha(TH_TEXT_DIM, a), ALIGN_LEFT, PANE_IW - 320, meta);

    const char *action = loading ? tr("Cross loads this slot") : tr("Cross saves to this slot");
    float hx = PANE_IX + PANE_IW - 28 - text_width(19, FONT_REGULAR, action);
    text_draw(hx, gy + gh - 54, 19, FONT_REGULAR, argb_alpha(TH_HINT, a), ALIGN_LEFT, action);
    draw_pad_glyph(GLYPH_CROSS, hx - 22, gy + gh - 43, 20);
}

static void pane_auto(float a, int autos, long *auto_ages)
{
    const float row_h = 78, gap = 12;
    for (int k = 0; k < autos; ++k)
    {
        const float y = PANE_IY + k * (row_h + gap);
        bool on = k == M.auto_choice;
        if (on)
        {
            draw_glow(PANE_IX - 6, y - 6, PANE_IW + 12, row_h + 12, 14,
                      argb_alpha(TH_FOCUS, 0.10f * a));
            draw_rrect_outline(PANE_IX, y, PANE_IW, row_h, 14, 2.0f,
                               argb_alpha(TH_FOCUS, 0.65f * a));
        }
        draw_rrect(PANE_IX, y, PANE_IW, row_h, 14,
                   argb_alpha(on ? TH_ROW_SELECTED : TH_BG_DEEP, a));
        icon_draw(ICON_CLOCK, PANE_IX + 24, y + 26, 26,
                  on ? argb_alpha(TH_FOCUS, a) : argb_alpha(TH_TEXT_DIM, a));
        char title[64], ago[48];
        snprintf(title, sizeof(title), tr("Auto-save %d"), k + 1);
        long age = auto_ages[k];
        if (age < 60)
            str_copy(ago, sizeof(ago), tr("Just now"));
        else if (age < 3600)
            snprintf(ago, sizeof(ago), tr("%ld min ago"), age / 60);
        else if (age < 86400)
            snprintf(ago, sizeof(ago), tr("%ld h ago"), age / 3600);
        else
            snprintf(ago, sizeof(ago), tr("%ld days ago"), age / 86400);
        text_draw(PANE_IX + 66, y + 16, 24, on ? FONT_BOLD : FONT_REGULAR,
                  argb_alpha(TH_TEXT, a), ALIGN_LEFT, title);
        text_draw(PANE_IX + 66, y + 46, 16, FONT_REGULAR, argb_alpha(TH_TEXT_DIM, a), ALIGN_LEFT,
                  ago);
        if (on)
            text_draw(PANE_IX + PANE_IW - 22, y + 28, 18, FONT_REGULAR, argb_alpha(TH_HINT, a),
                      ALIGN_RIGHT, tr("Cross loads it"));
    }
    if (autos > 1)
        text_draw(PANE_IX, PANE_IY + autos * (row_h + gap) + 10, 17, FONT_REGULAR,
                  argb_alpha(TH_HINT, a), ALIGN_LEFT, tr("Left / Right  Auto-save"));
}

/* One renderer for the cheat list, in the pane and on the Cheats screen. */
static void cheat_rows(CheatList *cl, float x, float y, float w, int rows, float row_h, float a,
                       bool animated)
{
    if (cl->count == 0)
        return;
    static int top_row;
    top_row = list_top_row(top_row, M.cheat_cursor, rows, cl->count);
    int first = top_row;
    if (animated)
    {
        anim_approach(&M.cheat_scroll, (float)top_row, app.dt, TH_SNAP);
        first = (int)floorf(M.cheat_scroll);
    }
    plat_set_clip((int)x - 8, (int)y - 8, (int)w + 16, (int)(rows * row_h) + 8);
    const float sel_y = y + (float)(M.cheat_cursor - first) * row_h;
    draw_glow(x - 6, sel_y - 6, w + 12, row_h + 12, 12, argb_alpha(TH_FOCUS, 0.08f * a));
    draw_rrect(x, sel_y, w, row_h - 8, 12, argb_alpha(TH_ROW_SELECTED, a));
    draw_rrect(x + 12, sel_y + row_h * 0.5f - 12, 4, 24, 2, argb_alpha(TH_FOCUS, a));
    draw_rrect_outline(x, sel_y, w, row_h - 8, 12, 2.0f, argb_alpha(TH_FOCUS, 0.65f * a));
    for (int i = first; i < cl->count && i < first + rows; ++i)
    {
        const Cheat *c = &cl->items[i];
        const float ry = y + (float)(i - first) * row_h;
        /* the track needs an edge: a bare switch on a dark card reads as a dot */
        draw_rrect_outline(x + 23, ry + (row_h - 8 - 30) * 0.5f - 1, 30 * 2.1f + 2, 32, 16, 1.0f,
                           argb_alpha(TH_TEXT_DIM, 0.24f * a));
        draw_switch(x + 24, ry + (row_h - 8 - 30) * 0.5f, 30, c->enabled ? 1.0f : 0.0f);
        float tag = 0;
        if (c->patch)
        {
            tag = text_width(15, FONT_BOLD, tr("Patch")) + 26;
            draw_rrect(x + w - 22 - tag, ry + (row_h - 8 - 26) * 0.5f, tag, 26, 13,
                       argb_alpha(TH_PILL, a));
            text_draw(x + w - 22 - tag * 0.5f, ry + (row_h - 8 - 20) * 0.5f, 15, FONT_BOLD,
                      argb_alpha(TH_FOCUS, a), ALIGN_CENTER, tr("Patch"));
        }
        text_draw_fit(x + 100, ry + (row_h - 8 - 24) * 0.5f, 23,
                      c->enabled ? FONT_BOLD : FONT_REGULAR,
                      argb_alpha(c->enabled ? TH_TEXT : TH_TEXT_SOFT, a), ALIGN_LEFT,
                      w - 134 - tag, c->desc);
    }
    plat_set_clip(0, 0, 0, 0);
}

static void pane_cheats(float a)
{
    CheatList *cl = &app.cheats;
    const int rows = 7;
    const float row_h = 58;
    if (cl->count == 0)
    {
        const float bx = PANE_IX, by = PANE_IY + 50, bw = PANE_IW;
        draw_rrect(bx, by, bw, 250, 18, argb_alpha(TH_BG_DEEP, 0.6f * a));
        draw_cart(bx + bw * 0.5f - 18, by + 42, 46, argb_alpha(TH_SWITCH_OFF, a),
                  argb_alpha(TH_BG_DEEP, a));
        text_draw(bx + bw * 0.5f, by + 108, 26, FONT_BOLD, argb_alpha(TH_TEXT, a), ALIGN_CENTER,
                  tr("No cheats for this game"));
        text_draw_fit(bx + bw * 0.5f, by + 150, 19, FONT_REGULAR, argb_alpha(TH_TEXT_DIM, a),
                      ALIGN_CENTER, bw - 100,
                      cheats_fetch_running()
                          ? tr("Fetching the game's cheat file...")
                          : tr("GBAR5 downloads the libretro cheat file when a game starts."));
        if (cheats_fetch_running())
            text_draw(bx + bw * 0.5f, by + 184, 17, FONT_REGULAR, argb_alpha(TH_HINT, a),
                      ALIGN_CENTER, tr("They appear here as soon as it lands."));
        return;
    }
    draw_rrect(PANE_IX - 12, PANE_IY - 12, PANE_IW + 24, (float)rows * row_h + 16, 16,
               argb_alpha(TH_BG_DEEP, 0.55f * a));
    cheat_rows(cl, PANE_IX, PANE_IY, PANE_IW, rows, row_h, a, false);

    /* the chosen code, in full: a cheat list without its codes is a guess */
    const float cy = PANE_IY + (float)rows * row_h + 14;
    draw_rect(PANE_IX, cy - 10, PANE_IW, 1, argb_alpha(TH_DIVIDER, a));
    const Cheat *c = &cl->items[M.cheat_cursor];
    text_draw(PANE_IX, cy + 2, 13, FONT_MONO, argb_alpha(TH_HINT, a), ALIGN_LEFT, tr("Code"));
    text_draw_fit(PANE_IX, cy + 24, 17, FONT_MONO, argb_alpha(TH_TEXT_DIM, a), ALIGN_LEFT, PANE_IW,
                  c->code);
    text_draw(PANE_IX + PANE_IW, cy + 2, 17, FONT_REGULAR, argb_alpha(TH_HINT, a), ALIGN_RIGHT,
              tr("Cross opens the cheat list"));
}

static void pane_disc(float a)
{
    char num[32];
    snprintf(num, sizeof(num), "%d / %d", M.disc_choice + 1, host_disc_count());
    const float cxx = PANE_IX + PANE_IW * 0.5f, cyy = PANE_IY + 190;
    draw_ring(cxx, cyy, 88, 2, argb_alpha(TH_DIVIDER, a));
    icon_draw(ICON_DISC, cxx - 40, cyy - 40, 80, argb_alpha(TH_FOCUS, a));
    text_draw(cxx, cyy + 116, 44, FONT_BOLD, argb_alpha(TH_TEXT, a), ALIGN_CENTER, num);
    text_draw(cxx, cyy + 178, 20, FONT_REGULAR, argb_alpha(TH_TEXT_DIM, a), ALIGN_CENTER,
              tr("Cross inserts this disc"));
    if (host_disc_count() > 1)
        text_draw(cxx, cyy + 212, 18, FONT_REGULAR, argb_alpha(TH_HINT, a), ALIGN_CENTER,
                  tr("Left / Right  Disc"));
}

static void pane_action(float a, int icon, const char *title, const char *blurb, uint32_t accent)
{
    const float cxx = PANE_IX + PANE_IW * 0.5f, cyy = PANE_IY + 176;
    draw_rrect(cxx - 44, cyy - 44, 88, 88, 24, argb_alpha(accent, 0.13f * a));
    draw_rrect_outline(cxx - 44, cyy - 44, 88, 88, 24, 1.5f, argb_alpha(accent, 0.35f * a));
    icon_draw(icon, cxx - 24, cyy - 24, 48, argb_alpha(accent, a));
    text_draw(cxx, cyy + 84, 30, FONT_BOLD, argb_alpha(TH_TEXT, a), ALIGN_CENTER, title);
    text_draw_fit(cxx, cyy + 132, 19, FONT_REGULAR, argb_alpha(TH_TEXT_DIM, a),
                  ALIGN_CENTER, PANE_IW - 120, blurb);
}

/* ---------------------------------------------------------------- the menu */

void menu_screen(uint32_t pressed)
{
    /* ------------------------------------------------ input */
    int before = M.cursor;
    int step = (pressed & BIT(BTN_DOWN)) ? 1 : (pressed & BIT(BTN_UP)) ? -1 : 0;
    if (step)
    {
        do
            M.cursor = (M.cursor + step + MI_COUNT) % MI_COUNT;
        while (!item_shown(M.cursor));
    }
    if (M.cursor != before)
    {
        sfx_play(SFX_CLICK);
        M.pane_t = 0.0f; /* the detail pane fades in with its new section */
    }
    bool on_slots = M.cursor == MI_SAVE || M.cursor == MI_LOAD;
    bool left = pressed & BIT(BTN_LEFT), right = pressed & BIT(BTN_RIGHT);
    if (on_slots && (left || right))
    {
        app.settings.state_slot = (app.settings.state_slot + (right ? 1 : 9)) % 10;
        app.global.state_slot = app.settings.state_slot;
        sfx_play(SFX_CLICK);
    }
    int auto_slots[AUTO_SLOTS];
    long auto_ages[AUTO_SLOTS];
    int autos = play_auto_list(auto_slots, auto_ages);
    if (M.auto_choice >= autos)
        M.auto_choice = 0;
    if (M.cursor == MI_AUTO && autos > 1 && (left || right))
    {
        M.auto_choice = (M.auto_choice + (right ? 1 : autos - 1)) % autos;
        sfx_play(SFX_CLICK);
    }
    int discs = host_disc_count();
    if (M.cursor == MI_DISC && discs > 1 && (left || right))
    {
        M.disc_choice = (M.disc_choice + (right ? 1 : discs - 1)) % discs;
        sfx_play(SFX_CLICK);
    }
    if (pressed & (BIT(BTN_CIRCLE) | BIT(BTN_MENU)))
    {
        sfx_play(SFX_BACK);
        app.screen = SCREEN_GAME;
        return;
    }
    if (pressed & BIT(BTN_CROSS))
    {
        char msg[96], st[PSXS5_PATH_MAX];
        app_state_path(st, sizeof(st), app.settings.state_slot);
        switch (M.cursor)
        {
        case MI_RESUME:
            app.screen = SCREEN_GAME;
            return;
        case MI_SAVE:
            make_dirs(app.paths.states);
            if (host_save_state(st))
            {
                play_save_thumb(st);
                snprintf(msg, sizeof(msg), tr("Saved to slot %d"), app.settings.state_slot);
                app.screen = SCREEN_GAME;
            }
            else
                snprintf(msg, sizeof(msg), tr("Couldn't save to slot %d"), app.settings.state_slot);
            app_toast(msg);
            sfx_play(SFX_SELECT);
            return;
        case MI_LOAD:
            if (ra_hardcore())
                str_copy(msg, sizeof(msg), tr("Not allowed in hardcore mode"));
            else if (host_load_state(st))
            {
                snprintf(msg, sizeof(msg), tr("Loaded slot %d"), app.settings.state_slot);
                app.screen = SCREEN_GAME;
            }
            else if (!path_exists(st))
                snprintf(msg, sizeof(msg), tr("Slot %d is empty"), app.settings.state_slot);
            else
            {
                char made_by[48];
                if (state_core_read(st, made_by, sizeof(made_by)) &&
                    strcmp(made_by, host_core_name()) != 0)
                    snprintf(msg, sizeof(msg),
                             tr("Saved with %s: choose it in Settings > System to load this slot"),
                             made_by);
                else
                    snprintf(msg, sizeof(msg), tr("Couldn't load slot %d"), app.settings.state_slot);
            }
            app_toast(msg);
            sfx_play(SFX_SELECT);
            return;
        case MI_AUTO:
        {
            char path[PSXS5_PATH_MAX];
            if (ra_hardcore())
                str_copy(msg, sizeof(msg), tr("Not allowed in hardcore mode"));
            else if (autos > 0)
            {
                play_auto_path(auto_slots[M.auto_choice], path, sizeof(path));
                if (host_load_state(path))
                {
                    str_copy(msg, sizeof(msg), tr("Auto-save loaded"));
                    app.screen = SCREEN_GAME;
                }
                else
                    str_copy(msg, sizeof(msg), tr("Could not load the auto-save"));
            }
            else
                msg[0] = '\0';
            if (msg[0])
                app_toast(msg);
            sfx_play(SFX_SELECT);
            return;
        }
        case MI_GUIDE:
            sfx_play(SFX_SELECT);
            guide_open();
            return;
        case MI_DISC:
            if (host_disc_select(M.disc_choice))
            {
                snprintf(msg, sizeof(msg), tr("Disc %d inserted"), M.disc_choice + 1);
                app.screen = SCREEN_GAME;
            }
            else
                str_copy(msg, sizeof(msg), tr("Disc change failed"));
            app_toast(msg);
            return;
        case MI_CHEATS:
            if (ra_hardcore())
            {
                app_toast("Cheats are off in hardcore mode");
                return;
            }
            M.cheat_cursor = 0;
            M.cheat_scroll = 0;
            M.cheat_y = 0;
            app.screen = SCREEN_CHEATS;
            sfx_play(SFX_SELECT);
            return;
        case MI_ACHIEVEMENTS:
            sfx_play(SFX_SELECT);
            achievements_open();
            return;
        case MI_MANUAL:
            sfx_play(SFX_SELECT);
            manual_open();
            return;
        case MI_SETTINGS:
            sfx_play(SFX_SELECT);
            app_open_settings(SCREEN_MENU);
            return;
        case MI_RESET:
            host_reset();
            ra_reset();
            app_toast("Console reset");
            app.screen = SCREEN_GAME;
            return;
        case MI_QUIT:
            app_save_settings();
            sfx_play(SFX_BACK);
            app_stop_game();
            shelf_select_game(app.global.last_game);
            return;
        default:
            break;
        }
    }

    /* ------------------------------------------------ draw */
    M.open_t = fminf(M.open_t + app.dt * 7.0f, 1.0f);
    const float ease = 1.0f - (1.0f - M.open_t) * (1.0f - M.open_t);
    M.pane_t = fminf(M.pane_t + app.dt * 9.0f, 1.0f);
    const float pane_a = ease * M.pane_t * M.pane_t * (3.0f - 2.0f * M.pane_t);

    app_draw_game((uint8_t)(255 - 170 * ease));
    draw_rect(0, 0, plat_width(), plat_height(), argb_alpha(TH_BG_DEEP, 0.55f * ease));

    deck_draw(ease);
    deck_draw_header(ease);
    draw_deck_list(ease, autos, discs);

    /* the pane's title and its right-hand note, per section */
    char meta[96] = "";
    switch (M.cursor)
    {
    case MI_SAVE:
    case MI_LOAD:
        snprintf(meta, sizeof(meta), tr("Slot %d of 10"), app.settings.state_slot + 1);
        break;
    case MI_AUTO:
        snprintf(meta, sizeof(meta), "%d", autos);
        break;
    case MI_CHEATS:
    {
        int on = 0;
        for (int k = 0; k < app.cheats.count; ++k)
            on += app.cheats.items[k].enabled;
        if (app.cheats.count)
            snprintf(meta, sizeof(meta), tr("%d of %d on"), on, app.cheats.count);
        break;
    }
    case MI_DISC:
        snprintf(meta, sizeof(meta), tr("%d of %d"), M.disc_choice + 1, discs);
        break;
    case MI_RESET:
    case MI_QUIT:
        str_copy(meta, sizeof(meta), tr("Careful"));
        break;
    case MI_SETTINGS:
        str_copy(meta, sizeof(meta), tr("Every game"));
        break;
    default:
        break;
    }
    draw_pane_frame(ease, tr(ITEMS[M.cursor].name), meta);
    switch (M.cursor)
    {
    case MI_SAVE:
        pane_slots(pane_a, false);
        break;
    case MI_LOAD:
        pane_slots(pane_a, true);
        break;
    case MI_AUTO:
        pane_auto(pane_a, autos, auto_ages);
        break;
    case MI_CHEATS:
        pane_cheats(pane_a);
        break;
    case MI_DISC:
        pane_disc(pane_a);
        break;
    case MI_MANUAL:
        pane_action(pane_a, ICON_BOOKS, tr("The game's manual"),
                    tr("Page images from the game's own folder."), TH_FOCUS);
        break;
    case MI_GUIDE:
        pane_action(pane_a, ICON_FILE_TEXT, tr("A guide beside the game"),
                    tr("Text files next to the game, in columns."), TH_FOCUS);
        break;
    case MI_ACHIEVEMENTS:
        pane_action(pane_a, ICON_TROPHY, tr("RetroAchievements"),
                    tr("What you have unlocked for this game."), TH_GOLD);
        break;
    case MI_SETTINGS:
        pane_action(pane_a, ICON_ADJUSTMENTS, tr("Screen, controls, sound"),
                    tr("Themes, scaling and pads - the shelf's Settings, from here."), TH_FOCUS);
        break;
    case MI_RESET:
        pane_action(pane_a, ICON_REFRESH, tr("Restart this game"),
                    tr("Unsaved progress since your last state is lost."), TH_DANGER);
        break;
    case MI_QUIT:
        pane_action(pane_a, ICON_DOOR_EXIT, tr("Back to the game library"),
                    tr("GBAR5 saves your settings and remembers this game."), TH_DANGER);
        break;
    default:
        pane_resume(pane_a);
        break;
    }

    static const int glyphs[] = {GLYPH_CROSS, GLYPH_CIRCLE};
    static const char *const labels[] = {"Select", "Resume"};
    const char *hint_right = on_slots ? tr("Left / Right  Slot")
                                 : M.cursor == MI_AUTO && autos > 1 ? tr("Left / Right  Auto-save")
                                 : M.cursor == MI_DISC && discs > 1 ? tr("Left / Right  Disc")
                                                                    : NULL;
    app_draw_hints(glyphs, labels, 2, hint_right);
    app_draw_toast();
}

/* ---------------------------------------------------------------- cheats */

void cheats_screen(uint32_t pressed)
{
    CheatList *cl = &app.cheats;
    const int rows = 7;
    const float row_h = 68;

    if (cl->count > 0)
    {
        int before = M.cheat_cursor;
        if (pressed & BIT(BTN_UP))
            M.cheat_cursor = (M.cheat_cursor + cl->count - 1) % cl->count;
        if (pressed & BIT(BTN_DOWN))
            M.cheat_cursor = (M.cheat_cursor + 1) % cl->count;
        if (pressed & BIT(BTN_L1))
            M.cheat_cursor = M.cheat_cursor > rows ? M.cheat_cursor - rows : 0;
        if (pressed & BIT(BTN_R1))
            M.cheat_cursor = M.cheat_cursor + rows < cl->count ? M.cheat_cursor + rows : cl->count - 1;
        if (M.cheat_cursor != before)
            sfx_play(SFX_CLICK);
        if (pressed & BIT(BTN_CROSS))
        {
            cl->items[M.cheat_cursor].enabled = !cl->items[M.cheat_cursor].enabled;
            cheats_apply(cl);
            cheats_save_selection(cl);
            sfx_play(SFX_SELECT);
        }
        if (pressed & BIT(BTN_SQUARE))
        {
            for (int i = 0; i < cl->count; ++i)
                cl->items[i].enabled = false;
            cheats_apply(cl);
            cheats_save_selection(cl);
            app_toast("All cheats off");
        }
    }
    if (pressed & BIT(BTN_TRIANGLE))
    {
        sfx_play(SFX_SELECT);
        cheat_search_open();
        return;
    }
    if (pressed & BIT(BTN_CIRCLE))
    {
        sfx_play(SFX_BACK);
        app.screen = SCREEN_MENU;
        return;
    }

    app_draw_game(40);
    draw_rect(0, 0, plat_width(), plat_height(), argb_alpha(TH_BG_DEEP, 0.72f));
    deck_draw(1.0f);
    deck_draw_header(1.0f);

    const float x = 112, y = deck_top(), w = plat_width() - 224,
                h = deck_bottom() - deck_top();
    draw_rrect(x, y, w, h, 22, argb_alpha(TH_CARD, 0.78f));
    draw_rrect_outline(x, y, w, h, 22, 1.0f, argb_alpha(TH_DIVIDER, 1.0f));
    text_draw(x + 36, y + 26, 26, FONT_BOLD, argb_alpha(TH_TEXT, 1.0f), ALIGN_LEFT, tr("Cheats"));
    int on = 0;
    for (int i = 0; i < cl->count; ++i)
        on += cl->items[i].enabled;
    char meta[96];
    if (cl->count)
        snprintf(meta, sizeof(meta), tr("%d of %d on"), on, cl->count);
    else
        meta[0] = '\0';
    if (meta[0])
        text_draw(x + w - 36, y + 32, 17, FONT_MONO, argb_alpha(TH_HINT, 1.0f), ALIGN_RIGHT, meta);
    draw_rect(x + 36, y + 78, w - 72, 1, argb_alpha(TH_DIVIDER, 1.0f));

    const float ix = x + 36, iw = w - 72, iy = y + 104;
    if (cl->count == 0)
    {
        draw_cart(ix + iw * 0.5f - 22, iy + 40, 56, argb_alpha(TH_SWITCH_OFF, 1.0f),
                  argb_alpha(TH_CARD, 1.0f));
        text_draw(ix + iw * 0.5f, iy + 130, 30, FONT_BOLD, argb_alpha(TH_TEXT, 1.0f), ALIGN_CENTER,
                  tr("No cheats for this game"));
        if (cheats_fetch_running())
        {
            text_draw(ix + iw * 0.5f, iy + 186, 22, FONT_REGULAR, argb_alpha(TH_TEXT, 1.0f),
                      ALIGN_CENTER, tr("Fetching the game's cheat file..."));
            text_draw(ix + iw * 0.5f, iy + 220, 20, FONT_REGULAR, argb_alpha(TH_TEXT_DIM, 1.0f),
                      ALIGN_CENTER, tr("They appear here as soon as it lands."));
        }
        else
        {
            char where[PSXS5_PATH_MAX + 64];
            snprintf(where, sizeof(where),
                     tr("No cheat file for this game. Put a .cht in %s, or one next to the game."),
                     app.paths.cheats);
            text_draw_fit(ix + iw * 0.5f, iy + 186, 20, FONT_REGULAR,
                          argb_alpha(TH_TEXT_DIM, 1.0f), ALIGN_CENTER, iw - 120, where);
            text_draw(ix + iw * 0.5f, iy + 220, 20, FONT_REGULAR, argb_alpha(TH_TEXT_DIM, 1.0f),
                      ALIGN_CENTER, tr("GBAR5 downloads the libretro cheat file when a game starts."));
        }
    }
    else
    {
        draw_rrect(ix - 12, iy - 12, iw + 24, (float)rows * row_h + 16, 16,
                   argb_alpha(TH_BG_DEEP, 0.55f));
        cheat_rows(cl, ix, iy, iw, rows, row_h, 1.0f, true);

        /* the code of the chosen cheat, and where the list came from */
        const float sy = iy + (float)rows * row_h + 6;
        draw_rect(ix, sy, iw, 1, argb_alpha(TH_DIVIDER, 1.0f));
        const Cheat *c = &cl->items[M.cheat_cursor];
        text_draw(ix, sy + 14, 13, FONT_MONO, argb_alpha(TH_HINT, 1.0f), ALIGN_LEFT, tr("Code"));
        text_draw_fit(ix + 52, sy + 10, 18, FONT_MONO, argb_alpha(TH_TEXT_DIM, 1.0f), ALIGN_LEFT,
                      iw - 52, c->code);
        const char *src = strrchr(cl->source, '/');
        if (cl->source[0])
            text_draw_fit(ix, sy + 42, 15, FONT_MONO, argb_alpha(TH_HINT, 1.0f), ALIGN_LEFT, iw,
                          src ? src + 1 : cl->source);
    }

    if (cl->count)
    {
        static const int glyphs[] = {GLYPH_CROSS, GLYPH_SQUARE, GLYPH_TRIANGLE, GLYPH_CIRCLE};
        static const char *const labels[] = {"Toggle", "All off", "Find a code", "Back"};
        app_draw_hints(glyphs, labels, 4, "L1 / R1  Page");
    }
    else
    {
        /* nothing to toggle: offer the one thing that can still help */
        static const int glyphs[] = {GLYPH_TRIANGLE, GLYPH_CIRCLE};
        static const char *const labels[] = {"Find a code", "Back"};
        app_draw_hints(glyphs, labels, 2, NULL);
    }
    app_draw_toast();
}

/* ---------------------------------------------------------------- achievements */

#define MAX_ACH 400

static struct
{
    RaAchievement list[MAX_ACH];
    int count, cursor;
    float scroll, sel_y;
    PlatTexture *badge[MAX_ACH];
    SDL_atomic_t badge_ready[MAX_ACH]; /* 1 = file on disk, 2 = no picture */
    SDL_Thread *fetcher;
    SDL_atomic_t stop;
} A;

static void badge_path(int i, char *out, size_t size)
{
    char file[48];
    snprintf(file, sizeof(file), "cache/badges/%u_%d.png", A.list[i].id, A.list[i].unlocked ? 1 : 0);
    path_join(out, size, app.paths.root, file);
}

/* downloads missing badge pictures, nearest first, off the main thread */
static int fetch_badges(void *unused)
{
    (void)unused;
    char dir[PSXS5_PATH_MAX];
    path_join(dir, sizeof(dir), app.paths.root, "cache/badges");
    make_dirs(dir);
    for (int i = 0; i < A.count && !SDL_AtomicGet(&A.stop); ++i)
    {
        char path[PSXS5_PATH_MAX];
        badge_path(i, path, sizeof(path));
        bool ok = path_exists(path) ||
                  (A.list[i].badge_url[0] && net_download(A.list[i].badge_url, path) == NET_OK);
        SDL_AtomicSet(&A.badge_ready[i], ok ? 1 : 2);
    }
    return 0;
}

static void stop_fetcher(void)
{
    if (A.fetcher)
    {
        SDL_AtomicSet(&A.stop, 1);
        SDL_WaitThread(A.fetcher, NULL);
        A.fetcher = NULL;
    }
    for (int i = 0; i < MAX_ACH; ++i)
    {
        plat_texture_free(A.badge[i]);
        A.badge[i] = NULL;
        SDL_AtomicSet(&A.badge_ready[i], 0);
    }
}

/* The list for the tab: "Achievable" (still locked, the closest first) or
 * "Achieved". view[] holds indices into A.list. */
static int ach_view[MAX_ACH], ach_view_count, ach_tab;

static void build_ach_view(void)
{
    ach_view_count = 0;
    for (int i = 0; i < A.count; ++i)
        if (A.list[i].unlocked == (ach_tab == 1))
            ach_view[ach_view_count++] = i;
    if (ach_tab == 0) /* insertion sort: furthest along first, list order otherwise */
        for (int i = 1; i < ach_view_count; ++i)
        {
            int v = ach_view[i], j = i;
            while (j > 0 && A.list[ach_view[j - 1]].percent < A.list[v].percent)
            {
                ach_view[j] = ach_view[j - 1];
                --j;
            }
            ach_view[j] = v;
        }
    A.cursor = 0;
    A.scroll = 0;
    A.sel_y = 0;
}

void achievements_open(void)
{
    stop_fetcher();
    A.count = ra_list(A.list, MAX_ACH);
    ach_tab = 0;
    build_ach_view();
    A.cursor = 0;
    A.scroll = 0;
    A.sel_y = 0;
    SDL_AtomicSet(&A.stop, 0);
    A.fetcher = A.count ? SDL_CreateThread(fetch_badges, "badges", NULL) : NULL;
    app.screen = SCREEN_ACHIEVEMENTS;
}

void achievements_screen(uint32_t pressed)
{
    const int rows = 7;
    const float row_h = 104, top = 214, x = 260, w = plat_width() - 520.0f;
    if (pressed & (BIT(BTN_L1) | BIT(BTN_R1) | BIT(BTN_LEFT) | BIT(BTN_RIGHT)))
    {
        ach_tab ^= 1;
        build_ach_view();
        sfx_play(SFX_CLICK);
    }
    const int count = ach_view_count;
    if (count)
    {
        int before = A.cursor;
        if (pressed & BIT(BTN_UP))
            A.cursor = (A.cursor + count - 1) % count;
        if (pressed & BIT(BTN_DOWN))
            A.cursor = (A.cursor + 1) % count;
        if (pressed & BIT(BTN_L2))
            A.cursor = A.cursor > rows ? A.cursor - rows : 0;
        if (pressed & BIT(BTN_R2))
            A.cursor = A.cursor + rows < count ? A.cursor + rows : count - 1;
        if (A.cursor != before)
            sfx_play(SFX_CLICK);
    }
    if (pressed & BIT(BTN_CIRCLE))
    {
        stop_fetcher();
        sfx_play(SFX_BACK);
        app.screen = SCREEN_MENU;
        return;
    }

    app_draw_game(40);
    draw_rect(0, 0, plat_width(), plat_height(), TH_SCRIM);
    text_draw(TH_MARGIN, 40, 44, FONT_BOLD, TH_TEXT, ALIGN_LEFT, tr("Achievements"));
    int unlocked = 0, points = 0, total_points = 0;
    for (int i = 0; i < A.count; ++i)
    {
        unlocked += A.list[i].unlocked;
        total_points += (int)A.list[i].points;
        points += A.list[i].unlocked ? (int)A.list[i].points : 0;
    }
    char sub[160];
    snprintf(sub, sizeof(sub), tr("%d of %d unlocked  \xc2\xb7  %d of %d points"), unlocked, A.count, points,
             total_points);
    text_draw(TH_MARGIN + 2, 100, 22, FONT_REGULAR, TH_TEXT_DIM, ALIGN_LEFT, sub);
    if (A.count)
    {
        float bw = 420, bx = plat_width() - TH_MARGIN - bw;
        draw_rrect(bx, 62, bw, 10, 5, TH_PILL);
        draw_rrect(bx, 62, bw * unlocked / A.count, 10, 5, TH_GOLD);
    }
    /* the two tabs */
    {
        char label[2][64];
        snprintf(label[0], sizeof(label[0]), "%s  %d", tr("Achievable"), A.count - unlocked);
        snprintf(label[1], sizeof(label[1]), "%s  %d", tr("Achieved"), unlocked);
        float tx = x;
        for (int t = 0; t < 2; ++t)
        {
            float tw = text_width(22, FONT_REGULAR, label[t]) + 44;
            bool on = t == ach_tab;
            draw_rrect(tx, 146, tw, 46, 23, on ? TH_TEXT : TH_PILL);
            text_draw(tx + tw * 0.5f, 156, 22, on ? FONT_BOLD : FONT_REGULAR, on ? TH_BG : TH_TEXT_DIM,
                      ALIGN_CENTER, label[t]);
            tx += tw + 12;
        }
    }

    static int top_row;
    top_row = list_top_row(top_row, A.cursor, rows, count);
    anim_approach(&A.scroll, (float)top_row, app.dt, TH_SNAP);
    draw_rrect(x - 16, top - 16, w + 32, rows * row_h + 24, TH_RADIUS, TH_CARD);
    plat_set_clip((int)x - 8, (int)top - 8, (int)w + 16, (int)(rows * row_h) + 8);
    if (count)
    {
        float target = top + (A.cursor - A.scroll) * row_h;
        if (A.sel_y == 0)
            A.sel_y = target;
        anim_approach(&A.sel_y, target, app.dt, TH_SNAP * 1.5f);
        draw_rrect(x, A.sel_y, w, row_h - 8, TH_RADIUS_SMALL, TH_ROW_SELECTED);
        draw_rrect_outline(x, A.sel_y, w, row_h - 8, TH_RADIUS_SMALL, 3, TH_FOCUS);
    }
    int first = (int)floorf(A.scroll);
    for (int k = first; k < count && k <= first + rows; ++k)
    {
        const int i = ach_view[k];
        const RaAchievement *a = &A.list[i];
        float y = top + (k - A.scroll) * row_h;
        /* the badge, once downloaded; a trophy or a lock until then */
        if (!A.badge[i] && SDL_AtomicGet(&A.badge_ready[i]) == 1)
        {
            char path[PSXS5_PATH_MAX];
            badge_path(i, path, sizeof(path));
            int bw, bh, comp;
            unsigned char *px = stbi_load(path, &bw, &bh, &comp, 4);
            if (px)
            {
                A.badge[i] = plat_texture_create(px, bw, bh, true);
                stbi_image_free(px);
            }
            else
                SDL_AtomicSet(&A.badge_ready[i], 2);
        }
        if (A.badge[i])
            plat_draw_texture(A.badge[i], x + 14, y + 10, 64, 64, a->unlocked ? 0xffffffffu : 0xff707070u, false);
        else
        {
            draw_rrect(x + 14, y + 10, 64, 64, 10, a->unlocked ? 0xff2a2410u : TH_PILL);
            icon_draw(a->unlocked ? ICON_TROPHY : ICON_LOCK, x + 26, y + 22, 40,
                      a->unlocked ? TH_GOLD : TH_TEXT_DIM);
        }
        text_draw_fit(x + 100, y + 12, 26, FONT_BOLD, a->unlocked ? TH_TEXT : TH_TEXT_SOFT, ALIGN_LEFT,
                      w - 300, a->title);
        text_draw_fit(x + 100, y + 48, 20, FONT_REGULAR, TH_TEXT_DIM, ALIGN_LEFT, w - 300, a->description);
        char pts[32];
        snprintf(pts, sizeof(pts), tr("%u points"), a->points);
        text_draw(x + w - 24, y + 14, 22, FONT_REGULAR, a->unlocked ? TH_GOLD : TH_TEXT_DIM, ALIGN_RIGHT, pts);
        if (a->unlocked)
            icon_draw(ICON_CIRCLE_CHECK, x + w - 54, y + 46, 30, TH_GOOD);
        else if (a->progress[0])
        {
            /* counted: 18/80 dragons, as a bar */
            char pc[48];
            snprintf(pc, sizeof(pc), "%s  \xc2\xb7  %d%%", a->progress, (int)(a->percent + 0.5f));
            text_draw(x + w - 24, y + 46, 20, FONT_REGULAR, TH_FOCUS, ALIGN_RIGHT, pc);
            float bx = x + 100, bw = w - 324, k = a->percent < 0 ? 0 : a->percent > 100 ? 1 : a->percent / 100.0f;
            draw_rrect(bx, y + 80, bw, 8, 4, TH_PILL);
            if (k > 0)
                draw_rrect(bx, y + 80, bw * k < 8 ? 8 : bw * k, 8, 4, TH_GOLD);
        }
    }
    plat_set_clip(0, 0, 0, 0);
    if (!A.count)
        text_draw(plat_width() * 0.5f, top + 200, 28, FONT_REGULAR, TH_TEXT_DIM, ALIGN_CENTER,
                  tr("No achievements loaded for this game"));
    else if (!count)
        text_draw(plat_width() * 0.5f, top + 200, 28, FONT_REGULAR, TH_TEXT_DIM, ALIGN_CENTER,
                  tr(ach_tab ? "None unlocked yet" : "All unlocked!"));
    static const int glyphs[] = {GLYPH_CIRCLE};
    static const char *const labels[] = {"Back"};
    app_draw_hints(glyphs, labels, 1, A.count ? "L1 / R1  Tab   \xc2\xb7   L2 / R2  Page" : NULL);
    app_draw_toast();
}
