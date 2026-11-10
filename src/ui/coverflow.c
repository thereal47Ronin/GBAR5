/*
 * GBAR5 - the cartridge shelf (home screen).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Games are Game Boy Advance cartridges standing on a shelf: the selected one
 * faces you, its neighbours sit either side, dimmed. A GBA has no discs, so
 * there is no disc, no disc case and no disc animation anywhere in here.
 * Everything is drawn with blits and fills, the software renderer's fast paths.
 */
#include "coverflow.h"

#include "../app.h"
#include "../art.h"
#include "../config.h"
#include "../covers.h"
#include "../gbadb.h"
#include "../gamedb.h"
#include "../play.h"
#include "../profiles.h"
#include "../i18n.h"
#include "../platform/platform.h"
#include "../ra/achievements.h"
#include "../stats.h"
#include "../tips.h"
#include "../update.h"
#include "draw.h"
#include "icons.h"
#include "sfx.h"
#include "text.h"
#include "theme.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CENTER_X 960.0f
#define CENTER_Y 430.0f
#define COVER_H 470.0f
#define SIDE_GAP 350.0f  /* centre to the first neighbour */
#define STACK_GAP 200.0f /* between further neighbours */
#define SIDE_SCALE 0.80f
#define SIDE_SQUEEZE 1.0f /* cartridges stay face-on: no fake 3D */
#define LAUNCH_TIME 0.22f /* a short fade, then the game. No animation, nothing popping up. */

static float launch_total(void)
{
    return LAUNCH_TIME;
}

static struct
{
    int *view; /* library indices on the shelf, in order */
    int view_count, view_capacity;
    int cursor; /* position in view */
    float pos;  /* animated position */
    bool details;
    float details_t, launch_t;
    bool starting; /* the launch is over: this frame shows "Loading", the next one loads */
    float chip_x, chip_w; /* animated category highlight */
    float tint[3];        /* animated background colour */
    float title_fade;     /* text fades in after a move */
    int last_cursor_game;
    /* "Continue or start over" when a game has a quick-resume save */
    bool dialog, resume;
    int dialog_choice;
    float dialog_t;
    long resume_age;
    PlatTexture *resume_thumb;
} S;

static const char *const CATEGORY_NAMES[CAT_COUNT] = {
    "All games", "Recently played", "Favorites", "Multi-game", "USA", "Europe", "Japan", "2+ players", "Hidden"};
static const char *const SORT_NAMES[SORT_COUNT] = {"Title", "Recently played", "Most played",
                                                   "Region"};

const char *shelf_sort_name(int sort)
{
    return tr(SORT_NAMES[sort >= 0 && sort < SORT_COUNT ? sort : 0]);
}

/* 0 unknown, 1 USA, 2 Europe, 3 Japan */
static int region_of(const char *serial)
{
    if (!serial[0])
        return 0;
    /* a GBA cartridge has no serial: the library scanner stores the No-Intro
     * region tag as U/E/J instead */
    if (!serial[1])
    {
        if (serial[0] == 'U')
            return 1;
        if (serial[0] == 'E')
            return 2;
        if (serial[0] == 'J')
            return 3;
    }
    if (!strncmp(serial, "SLUS", 4) || !strncmp(serial, "SCUS", 4) || !strncmp(serial, "PAPX", 4))
        return 1;
    if (!strncmp(serial, "SLES", 4) || !strncmp(serial, "SCES", 4) || !strncmp(serial, "SCED", 4))
        return 2;
    return 3;
}

const char *shelf_region_name(const char *serial)
{
    static const char *const names[] = {"Unknown region", "USA", "Europe", "Japan"};
    return tr(names[region_of(serial)]);
}

/* What the shelf knows about the game: DuckStation's database for a
 * PlayStation disc, the cartridge one for a Game Boy Advance game (loaded from
 * assets/gba-gamedb.txt the first time it is asked). NULL when neither has it. */
static const GameInfo *game_info(const Game *g)
{
    const GameInfo *info = gamedb_get(g->serial);
    if (info)
        return info;
    static GameInfo cartridge;
    return gbadb_get(g, &cartridge) ? &cartridge : NULL;
}

/* ---------------------------------------------------------------- the view */

static bool in_category(const Game *g, int category)
{
    GameStats *st = stats_get(g->id);
    if (st && st->hidden) /* hidden games are only under Hidden */
        return category == CAT_HIDDEN;
    switch (category)
    {
    case CAT_HIDDEN: return false;
    case CAT_RECENT: return st && st->last_played > 0;
    case CAT_FAVORITES: return st && st->favorite;
    case CAT_MULTI_DISC: return g->discs > 1;
    case CAT_USA: return region_of(g->serial) == 1;
    case CAT_EUROPE: return region_of(g->serial) == 2;
    case CAT_JAPAN: return region_of(g->serial) == 3;
    case CAT_MULTIPLAYER:
    {
        const GameInfo *info = game_info(g);
        return info && info->max_players > 1;
    }
    default: return true;
    }
}

static int category_size(int category)
{
    int n = 0;
    for (int i = 0; i < app.library.count; ++i)
        n += in_category(&app.library.games[i], category);
    return n;
}

static int sort_mode;

static int compare(const void *pa, const void *pb)
{
    const Game *a = &app.library.games[*(const int *)pa], *b = &app.library.games[*(const int *)pb];
    GameStats *sa = stats_get(a->id), *sb = stats_get(b->id);
    switch (sort_mode)
    {
    case SORT_RECENT:
    {
        int64_t la = sa ? sa->last_played : 0, lb = sb ? sb->last_played : 0;
        if (la != lb)
            return la < lb ? 1 : -1;
        break;
    }
    case SORT_MOST_PLAYED:
    {
        uint32_t ta = sa ? sa->seconds : 0, tb = sb ? sb->seconds : 0;
        if (ta != tb)
            return ta < tb ? 1 : -1;
        break;
    }
    case SORT_REGION:
    {
        int ra = region_of(a->serial), rb = region_of(b->serial);
        if (ra != rb)
            return ra - rb;
        break;
    }
    default:
        break;
    }
    int t = str_icmp(a->title, b->title);
    return t ? t : (*(const int *)pa - *(const int *)pb);
}

static void build_view(int keep_game)
{
    Settings *g = &app.global;
    if (g->shelf_category != CAT_ALL && category_size(g->shelf_category) == 0)
        g->shelf_category = CAT_ALL;
    if (S.view_capacity < app.library.count)
    {
        int *grown = realloc(S.view, sizeof(int) * (size_t)(app.library.count + 16));
        if (!grown)
            return;
        S.view = grown;
        S.view_capacity = app.library.count + 16;
    }
    S.view_count = 0;
    for (int i = 0; i < app.library.count; ++i)
        if (in_category(&app.library.games[i], g->shelf_category))
            S.view[S.view_count++] = i;
    sort_mode = g->shelf_category == CAT_RECENT ? SORT_RECENT : g->sort_mode;
    qsort(S.view, (size_t)S.view_count, sizeof(int), compare);
    S.cursor = 0;
    for (int k = 0; k < S.view_count; ++k)
        if (S.view[k] == keep_game)
            S.cursor = k;
    S.pos = (float)S.cursor;
}

static int selected_game(void)
{
    return S.view_count ? S.view[S.cursor] : -1;
}

void shelf_init(int last_game)
{
    S.last_cursor_game = last_game;
    srand((unsigned)time(NULL)); /* Surprise me */
    S.tint[0] = 0x3a / 255.0f;
    S.tint[1] = 0x50 / 255.0f;
    S.tint[2] = 0xc8 / 255.0f;
}

void shelf_library_changed(void)
{
    build_view(S.last_cursor_game);
}

void shelf_select_game(int library_index)
{
    S.last_cursor_game = library_index;
    build_view(library_index);
}

/* ---------------------------------------------------------------- alphabet jump */

/* A title's first letter, upper-cased: everything that isn't one (numbers, symbols)
 * shares a '#' group, which is where those games sit under "Title". */
static char title_initial(const char *title)
{
    while (*title == ' ' || *title == '\t')
        ++title;
    char c = *title;
    if (c >= 'a' && c <= 'z')
        c = (char)(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z')
        return c;
    return '#';
}

/* Left / Right walk the alphabet instead of the shelf: from the selected game,
 * to the first game of the next (+1) or previous (-1) letter. Stopping on a
 * group's first game means holding the button visits A, B, C... and back, not
 * one game per press. */
static void alphabet_jump(int dir)
{
    if (S.view_count < 2)
        return;
    if (S.cursor < 0)
        S.cursor = 0;
    else if (S.cursor >= S.view_count)
        S.cursor = S.view_count - 1;
    char from = title_initial(app.library.games[S.view[S.cursor]].title);
    int i = S.cursor + dir;
    while (i >= 0 && i < S.view_count && title_initial(app.library.games[S.view[i]].title) == from)
        i += dir;
    if (i < 0 || i >= S.view_count)
        return; /* that was the first or the last group */
    char to = title_initial(app.library.games[S.view[i]].title);
    while (i - 1 >= 0 && title_initial(app.library.games[S.view[i - 1]].title) == to)
        --i;
    S.cursor = i;
}

/* The last letter jump: holding Left / Right steps letter by letter at a pace
 * you can stop on, instead of the shelf's faster key repeat. */
static uint64_t letter_at;

/* ---------------------------------------------------------------- backdrop */

/* A neutral vertical gradient, built once and blitted tinted each frame:
 * a full-screen gradient mesh per frame would be costly here. */
/* The theme's background as a grey picture; plat_draw_backdrop colours it
 * (the cover's colour for Classic, the theme's otherwise). */
/* GBAR5's background: a deep emerald field with the GBA's dot-matrix texture,
 * a soft pool of light behind the screen and a vignette. Built once as a grey
 * field and tinted by the GPU per frame -- no downloaded art, no logos. */
static void make_backdrop(uint8_t *grey, int W, int H)
{
    for (int y = 0; y < H; ++y)
    {
        float t = (float)y / (H - 1);
        for (int x = 0; x < W; ++x)
        {
            float dx = (x - W * 0.5f) / (W * 0.5f);
            /* dark at the top, a lit band behind the cartridges, dark floor */
            float v = t < 0.55f ? 0.30f + t / 0.55f * 0.55f : 0.85f - (t - 0.55f) / 0.45f * 0.62f;
            /* the pool of light in the middle, where the chosen game sits */
            float gx = dx / 0.85f, gy = (t - 0.46f) / 0.55f;
            v += 0.45f * expf(-(gx * gx + gy * gy) * 2.4f);
            /* dot-matrix screen: a faint grid on the whole surface */
            int gxm = (x + 2) % 4, gym = (y + 2) % 4;
            if (gxm == 0 || gym == 0)
                v *= 0.92f;
            /* vignette so the edges fall away */
            v *= 1.0f - dx * dx * 0.30f;
            v = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
            grey[(size_t)y * W + x] = (uint8_t)(v * 255.0f);
        }
    }
}

void shelf_backdrop(void)
{
    enum { W = 960, H = 540 };
    static uint8_t *grey;
    static bool made;
    if (!grey)
        grey = malloc((size_t)W * H);
    if (!grey)
    {
        draw_rect(0, 0, plat_width(), plat_height(), TH_BG);
        return;
    }
    if (!made)
    {
        make_backdrop(grey, W, H);
        made = true;
    }
    /* the theme's own tint, so a custom palette tints the room too */
    plat_draw_backdrop(grey, W, H, theme.backdrop_tint ? theme.backdrop_tint : TH_BG);
}

static void update_tint(int game)
{
    /* The theme's backdrop tint, with a hint of the cartridge. The theme's own
     * colour: that tint pulled most of the way towards the cover. */
    bool dark = app.global.background == 0;
    uint32_t base = theme.backdrop_tint;
    float target[3] = {((base >> 16) & 0xff) / 255.0f, ((base >> 8) & 0xff) / 255.0f,
                       (base & 0xff) / 255.0f};
    if (dark)
        for (int i = 0; i < 3; ++i)
            target[i] *= 0.40f; /* deep emerald: the dim version of the tint */
    uint32_t c = game >= 0 ? covers_color(game) : 0;
    if (c)
    {
        float cover[3] = {((c >> 16) & 0xff) / 255.0f, ((c >> 8) & 0xff) / 255.0f, (c & 0xff) / 255.0f};
        float m = fmaxf(cover[0], fmaxf(cover[1], cover[2]));
        float mix = dark ? 0.22f : 0.55f, level = dark ? 0.32f : 0.8f;
        for (int i = 0; i < 3; ++i)
        {
            float v = m > 0.05f ? cover[i] / m * level : level * 0.6f; /* keep it bright enough */
            target[i] = target[i] * (1.0f - mix) + v * mix;
        }
    }
    for (int i = 0; i < 3; ++i)
        anim_approach(&S.tint[i], target[i], app.dt, 4.0f);
}

/* ---------------------------------------------------------------- the game's picture */

/* The selected game's in-game screenshot (art.c: libretro-thumbnails, cached on
 * the console) fills the screen behind the shelf, so a game can be judged
 * before it is started. The dimming is baked into a small copy of the picture
 * once per game (art_wash): stretching that across the screen with blending
 * off costs a couple of milliseconds, where scaling and blending the full
 * picture every frame costs ~25 ms on the console. It crossfades when the
 * selection moves. A game with no picture leaves the theme's own backdrop. */
static int picture_game = -1, picture_prev = -1;
static float picture_fade = 1.0f;

static void draw_shelf_picture(int game)
{
    if (game != picture_game)
    {
        picture_prev = picture_game;
        picture_game = game;
        picture_fade = 0.0f;
    }
    picture_fade = fminf(picture_fade + app.dt * 4.0f, 1.0f);

    const float sw = (float)plat_width(), sh = (float)plat_height();
    /* the dark themes keep a third of the picture; the light one is a pale wash */
    uint8_t keep = theme.light ? 26 : 70;
    uint32_t toward = theme.light ? (theme.bg & 0xffffffu) : 0x000000u;

    if (picture_prev >= 0 && picture_fade < 1.0f)
    {
        PlatTexture *old = art_wash(&app.library.games[picture_prev], keep, toward);
        if (old)
            plat_draw_texture(old, 0, 0, sw, sh, 0xffffffffu, false);
    }
    PlatTexture *pic = art_wash(&app.library.games[game], keep, toward);
    if (!pic)
        return; /* the theme's own backdrop is already there */
    if (picture_fade < 1.0f)
        plat_draw_texture(pic, 0, 0, sw, sh, argb_alpha(0xffffffffu, picture_fade), true);
    else
        plat_draw_texture(pic, 0, 0, sw, sh, 0xffffffffu, false);
}

/* ---------------------------------------------------------------- pieces */

/* RetroAchievements progress in a cover's bottom-right corner: gold once
 * mastered. Nothing for games without achievements or not played yet. */
static void draw_ach_badge(const Game *g, float right, float bottom, float size)
{
    GameStats *st = stats_get(g->id);
    if (!st || st->ach_total <= 0 || st->ach_unlocked < 0)
        return;
    bool done = st->ach_unlocked >= st->ach_total;
    char t[24];
    snprintf(t, sizeof(t), "%d/%d", st->ach_unlocked, st->ach_total);
    float h = size * 1.7f, tw = text_width(size, FONT_BOLD, t) + h + size * 0.9f;
    float x = right - tw - size * 0.4f, y = bottom - h - size * 0.4f;
    draw_rrect(x, y, tw, h, h * 0.5f, done ? 0xf0c8961eu : 0xd0101018u);
    icon_draw(ICON_TROPHY, x + h * 0.22f, y + h * 0.18f, h * 0.64f, done ? 0xffffffffu : 0xfff0b429u);
    text_draw(x + tw - size * 0.6f, y + (h - size) * 0.5f - 1, size, FONT_BOLD, 0xffffffffu, ALIGN_RIGHT, t);
}

/* The achievement line: the cup at the left margin, one slim bar running from
 * it to the count, and the count sitting on the card's right edge. All three
 * share a centre line, so nothing floats off beside the card. The cup is
 * aligned by its ink, not its box, to the same column as the fact labels above
 * it: label_size is those labels' size, and their ink starts a couple of
 * pixels right of the box. */
static void draw_ach_row(float left, float right, float cy, float label_size, int unlocked, int total,
                         float a)
{
    char count[24];
    snprintf(count, sizeof(count), "%d/%d", unlocked, total);
    float tw = text_width(19, FONT_BOLD, count);
    float bar_x = left + 38.0f, bar_right = right - tw - 18.0f;
    if (bar_right < bar_x + 60.0f)
        bar_right = bar_x + 60.0f;
    icon_draw_ink_left(ICON_TROPHY, left + text_ink_left(label_size, FONT_REGULAR), cy, 26,
                       argb_alpha(TH_GOLD, a));
    draw_rrect(bar_x, cy - 4.0f, bar_right - bar_x, 8, 4, argb_alpha(TH_TEXT_DIM, 0.28f * a));
    float f = total > 0 ? (float)unlocked / (float)total : 0.0f;
    f = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
    float fw = (bar_right - bar_x) * f;
    if (unlocked > 0 && fw < 8.0f)
        fw = 8.0f; /* 1/60 reads as the start of a bar, not a stray dot */
    if (fw > 0.0f)
        draw_rrect(bar_x, cy - 4.0f, fw, 8, 4, argb_alpha(TH_GOLD, a));
    text_draw(right, cy - 11.0f, 19, FONT_BOLD, argb_alpha(TH_TEXT_SOFT, a), ALIGN_RIGHT, count);
}

/* A Game Boy Advance cartridge: the shell, the label recess with the box art
 * in it, and the grip ridges at the bottom. Portrait, because that is how a
 * cartridge reads on a shelf. */
static void draw_cover(PlatTexture *tex, const Game *g, float cx, float cy, float h, float squeeze,
                       uint32_t tint, bool selected)
{
    (void)squeeze;
    int tw = 1, th = 1;
    if (tex)
        plat_texture_size(tex, &tw, &th);
    const float w = h * 0.70f, x = cx - w * 0.5f, y = cy - h * 0.5f;

    /* the shell: an original emerald cartridge, not a replica of anyone's */
    const uint32_t shell = theme.light ? 0xffcfe3d7u : 0xff143a28u;
    const uint32_t shell_hi = theme.light ? 0xffe6f2eau : 0xff1d5038u;
    const uint32_t groove = theme.light ? 0xffadc9b9u : 0xff0b2418u;

    /* the shadow it casts on the shelf */
    draw_rrect(x + 5, y + 14, w, h, 20, 0x78000000u);

    if (selected) /* a soft emerald halo so the chosen cartridge reads at a glance */
    {
        draw_rrect_outline(x - 14, y - 14, w + 28, h + 28, 26, 9, argb_alpha(TH_FOCUS, 0.35f));
        draw_rrect_outline(x - 6, y - 6, w + 12, h + 12, 22, 3, (theme.cover_outline & 0xffffffu) | 0xe0000000u);
    }

    /* the shell: one soft highlight down the left */
    draw_rrect(x, y, w, h, 20, shell);
    draw_rrect(x + 3, y + 3, w - 6, h - 6, 17, shell_hi);
    draw_rrect(x + 3, y + 3, w * 0.10f, h - 6, 8, 0x18ffffffu);

    /* the label recess: a dark inset, the box art inside it, a hairline frame */
    const float lx = x + w * 0.065f, ly = y + h * 0.055f;
    const float lw = w * 0.87f, lh = h * 0.68f;
    draw_rrect(lx - 4, ly - 4, lw + 8, lh + 8, 10, groove);
    draw_rect(lx, ly, lw, lh, theme.light ? 0xfff2f7f3u : 0xff08150eu);
    if (tex)
    {
        float k = fminf(lw / tw, lh / th), dw = tw * k, dh = th * k;
        plat_draw_texture(tex, lx + (lw - dw) * 0.5f, ly + (lh - dh) * 0.5f, dw, dh, tint,
                          app.global.cover_style == COVER_BOX3D);
    }
    else
        text_draw_fit(lx + lw * 0.5f, ly + lh * 0.5f - 14, 22, FONT_BOLD, tint, ALIGN_CENTER, lw - 20, g->title);
    draw_rrect_outline(lx - 4, ly - 4, lw + 8, lh + 8, 10, 2, 0x30ffffffu);

    /* the grip at the bottom, with its ridges: no printing, nothing borrowed */
    draw_rrect(x + w * 0.05f, y + h * 0.815f, w * 0.90f, h * 0.145f, 8, groove);
    for (int i = 0; i < 3; ++i)
        draw_rect(x + w * 0.21f, y + h * 0.845f + i * (h * 0.032f), w * 0.58f, 2, 0x34ffffffu);

    if (selected)
        draw_ach_badge(g, x + w, y + h, 18);
}

static void draw_details(const Game *g, float t)
{
    if (t <= 0.0f)
        return;
    /* dim what is behind it, so the facts panel reads on its own */
    draw_rect(0, 0, plat_width(), plat_height(), argb_alpha(0xff000000u, t * 0.55f));
    float ease = 1.0f - (1.0f - t) * (1.0f - t);
    float w = 620, x = plat_width() - (w + 48) * ease, y = 130, h = 830;
    draw_rrect(x, y, w, h, TH_RADIUS, argb_alpha(TH_CARD_A(theme.light ? 0xff : 0xf2), t));
    text_draw_fit(x + 40, y + 34, 32, FONT_BOLD, argb_alpha(TH_TEXT, t), ALIGN_LEFT, w - 80, g->title);
    GameStats *st = stats_get(g->id);
    char played[48] = "", when[48] = "", ach[48] = "", discs[16];
    if (st)
    {
        stats_format_time(st->seconds, played, sizeof(played));
        stats_format_when(st->last_played, when, sizeof(when));
        if (st->ach_unlocked >= 0 && st->ach_total > 0)
            snprintf(ach, sizeof(ach), "%d / %d", st->ach_unlocked, st->ach_total);
    }
    snprintf(discs, sizeof(discs), "%d", g->discs);
    const char *folder = strrchr(g->folder, '/');
    /* what the game databases know: genre, release, players */
    const GameInfo *info = game_info(g);
    char released[160] = "", players[48] = "";
    if (info && info->released[0])
        str_copy(released, sizeof(released), info->released); /* a cartridge's day, e.g. 2001-11-11 */
    else if (info && info->year)
        snprintf(released, sizeof(released), info->developer[0] ? "%d  \xc2\xb7  %s" : "%d", info->year, info->developer);
    if (info && info->max_players > 1)
        snprintf(players, sizeof(players), tr(info->flags & GDB_MULTITAP ? "1 to %d (multitap)" : "1 to %d"),
                 info->max_players);
    else if (info && info->max_players == 1)
        str_copy(players, sizeof(players), "1");
    struct { const char *label; const char *value; int icon; } rows[] = {
        {"Region", shelf_region_name(g->serial), ICON_WORLD},
        {"Genre", info && info->genre[0] ? info->genre : tr("Unknown"), ICON_CATEGORY},
        {"Released", released[0] ? released : tr("Unknown"), ICON_CLOCK},
        {"Played", played[0] ? played : tr("Not yet"), ICON_HISTORY},
        {"Achievements", ach[0] ? ach : tr("Unknown"), ICON_TROPHY},
        {"Folder", folder ? folder + 1 : g->folder, ICON_FOLDER},
    };
    (void)when;
    (void)players;
    (void)discs;
    for (int i = 0; i < 6; ++i)
    {
        float ry = y + 92 + i * 44;
        icon_draw(rows[i].icon, x + 40, ry + 1, 26, argb_alpha(TH_FOCUS, t));
        text_draw(x + 84, ry, 22, FONT_REGULAR, argb_alpha(TH_TEXT_DIM, t), ALIGN_LEFT, tr(rows[i].label));
        text_draw_fit(x + 310, ry, 22, FONT_BOLD, argb_alpha(TH_TEXT, t), ALIGN_LEFT, w - 350,
                      rows[i].value);
    }
    /* tips: worked out once per game (they read files), with its own settings */
    static char tips[3][TIP_LEN];
    static int tip_count;
    static const Game *tips_game;
    if (g != tips_game)
    {
        tips_game = g;
        Settings s;
        char own[PSXS5_PATH_MAX];
        app_game_config_path(own, sizeof(own), g);
        config_load_game(&s, &app.global, own);
        tip_count = tips_for(g, &s, tips, 3);
    }
    for (int i = 0; i < tip_count; ++i)
    {
        float ty = y + 380 + i * 34;
        icon_draw(ICON_INFO_CIRCLE, x + 40, ty + 2, 24, argb_alpha(TH_GOLD, t));
        text_draw_fit(x + 76, ty, 19, FONT_REGULAR, argb_alpha(TH_TEXT_SOFT, t), ALIGN_LEFT, w - 116, tips[i]);
    }
    /* the cartridge itself, so the panel ends on something */
    draw_cover(covers_get((int)(g - app.library.games)), g, x + w * 0.5f, y + 620, 250, 1.0f,
               argb_alpha(0xffffffffu, t), false);
    if (t > 0.5f)
        draw_pad_glyph(GLYPH_SQUARE, x + 54, y + h - 46, 28);
    text_draw(x + 78, y + h - 58, 22, FONT_REGULAR, argb_alpha(TH_TEXT, t), ALIGN_LEFT,
              tr("Choose a cover"));
}

/* Category chips along the top; the highlight slides between them. */
static void draw_header(void)
{
    text_draw(TH_MARGIN, 40, 44, FONT_BOLD, TH_TEXT, ALIGN_LEFT, "GBAR5");
    float title_w = text_width(44, FONT_BOLD, "GBAR5");
    text_draw(TH_MARGIN + title_w + 16, 62, 18, FONT_REGULAR, TH_HINT, ALIGN_LEFT, PSXS5_NAME);
    float x = TH_MARGIN + title_w + 16 + text_width(18, FONT_REGULAR, PSXS5_NAME) + 34, y = 46, h = 46;
    for (int c = 0; c < CAT_COUNT; ++c)
    {
        int n = category_size(c);
        if (c != CAT_ALL && n == 0)
            continue;
        char label[64];
        if (c == CAT_ALL)
            snprintf(label, sizeof(label), "%s  %d", tr(CATEGORY_NAMES[c]), n);
        else
            str_copy(label, sizeof(label), tr(CATEGORY_NAMES[c]));
        float w = text_width(22, FONT_REGULAR, label) + 40;
        bool on = c == app.global.shelf_category;
        if (on)
        {
            if (S.chip_w == 0)
                S.chip_x = x, S.chip_w = w;
            anim_approach(&S.chip_x, x, app.dt, TH_SNAP);
            anim_approach(&S.chip_w, w, app.dt, TH_SNAP);
            draw_rrect(S.chip_x, y, S.chip_w, h, h * 0.5f, TH_FOCUS);
        }
        text_draw(x + 20, y + 11, 22, FONT_REGULAR, on ? TH_BG : TH_HINT, ALIGN_LEFT, label);
        x += w + 6;
    }

    /* account and clock, top right */
    char clock_text[16];
    plat_clock(clock_text, sizeof(clock_text)); /* the console's time zone and 12/24 h */
    float rx = plat_width() - TH_MARGIN;
    text_draw(rx, 53, 24, FONT_REGULAR, TH_TEXT_SOFT, ALIGN_RIGHT, clock_text);
    rx -= text_width(24, FONT_REGULAR, clock_text) + 32;
    if (update_state() == UPDATE_AVAILABLE || update_state() == UPDATE_INSTALLED)
    {
        char up[64];
        snprintf(up, sizeof(up), tr(update_state() == UPDATE_INSTALLED ? "Restart for %s" : "Update %s"),
                 update_version());
        float uw = text_width(20, FONT_BOLD, up) + 64;
        draw_rrect(rx - uw, 44, uw, 46, 23, 0xff2a2410u);
        icon_draw(ICON_DOWNLOAD, rx - uw + 14, 52, 30, TH_GOLD);
        text_draw(rx - uw + 50, 56, 20, FONT_BOLD, TH_GOLD, ALIGN_LEFT, up);
        rx -= uw + 24;
    }
    if (profiles_count() > 1)
    {
        const char *name = profiles_name(profiles_current());
        float pw = text_width(22, FONT_BOLD, name) + 66;
        draw_rrect(rx - pw, 44, pw, 46, 23, TH_PILL);
        icon_draw(ICON_USER, rx - pw + 14, 52, 28, TH_FOCUS);
        text_draw(rx - pw + 50, 56, 22, FONT_BOLD, TH_TEXT, ALIGN_LEFT, name);
        rx -= pw + 24;
    }
    if (ra_user()[0])
    {
        char who[96];
        snprintf(who, sizeof(who), "%s  \xc2\xb7  %u", ra_user(), ra_user_score());
        text_draw(rx, 53, 24, FONT_REGULAR, TH_TEXT_SOFT, ALIGN_RIGHT, who);
        rx -= text_width(24, FONT_REGULAR, who) + 38;
        icon_draw(ICON_TROPHY, rx, 52, 30, TH_GOLD);
    }
}

static void draw_info(const Game *g, float alpha)
{
    GameStats *st = stats_get(g->id);
    float y = 730;
    float tw = text_width(52, FONT_BOLD, g->title);
    if (st && st->favorite)
        icon_draw(ICON_STAR, CENTER_X - fminf(tw, 1500) * 0.5f - 52, y + 10, 40,
                  argb_alpha(TH_GOLD, alpha));
    text_draw_fit(CENTER_X, y, 52, FONT_BOLD, argb_alpha(TH_TEXT, alpha), ALIGN_CENTER, 1500, g->title);

    /* tags: region, serial, discs, play time, last played */
    char discs[32], played[48] = "", played_tag[64] = "", when[48] = "";
    snprintf(discs, sizeof(discs), "%s", tr("GBA cartridge"));
    if (st)
    {
        stats_format_time(st->seconds, played, sizeof(played));
        if (played[0])
            snprintf(played_tag, sizeof(played_tag), tr("Played %s"), played);
        stats_format_when(st->last_played, when, sizeof(when));
    }
    (void)discs;
    const char *tags[5] = {shelf_region_name(g->serial), tr("GBA cartridge"), played_tag, when, ""};
    float widths[5], total = 0;
    for (int i = 0; i < 5; ++i)
        if (tags[i][0])
        {
            widths[i] = text_width(22, FONT_REGULAR, tags[i]) + 36;
            total += widths[i] + 10;
        }
        else
            widths[i] = 0;
    float x = CENTER_X - (total - 10) * 0.5f, ty = y + 84;
    for (int i = 0; i < 5; ++i)
        if (widths[i] > 0)
            x += draw_pill(x, ty, 40, 22, argb_alpha(TH_PILL_A(0xc0), alpha),
                           argb_alpha(TH_TEXT_SOFT, alpha), tags[i]) + 10;

    /* achievement progress, when known */
    if (st && st->ach_unlocked >= 0 && st->ach_total > 0)
    {
        float bw = 340, bx = CENTER_X - bw * 0.5f + 20, by = ty + 70;
        icon_draw(ICON_TROPHY, bx - 46, by - 12, 30, argb_alpha(TH_GOLD, alpha));
        draw_rrect(bx, by, bw, 8, 4, argb_alpha(TH_PILL, alpha));
        float f = (float)st->ach_unlocked / st->ach_total;
        draw_rrect(bx, by, bw * (f > 1 ? 1 : f), 8, 4, argb_alpha(TH_GOLD, alpha));
        char a[32];
        snprintf(a, sizeof(a), "%d / %d", st->ach_unlocked, st->ach_total);
        text_draw(bx + bw + 16, by - 13, 22, FONT_REGULAR, argb_alpha(TH_TEXT_SOFT, alpha), ALIGN_LEFT, a);
    }
}

/* ---------------------------------------------------------------- screen */

static void storage_screen(uint32_t pressed)
{
    shelf_backdrop();
    text_draw(TH_MARGIN, 40, 44, FONT_BOLD, TH_TEXT, ALIGN_LEFT, PSXS5_NAME);
    float w = 1200, h = 300, x = CENTER_X - w * 0.5f, y = 330;
    draw_rrect(x, y, w, h, TH_RADIUS, TH_CARD_SOFT);
    icon_draw(ICON_ALERT_TRIANGLE, CENTER_X - 32, y + 40, 64, TH_GOLD);
    text_draw(CENTER_X, y + 124, 36, FONT_BOLD, TH_TEXT, ALIGN_CENTER, tr("Game Boy Advance can't open /data/GBAR5"));
    text_draw_fit(CENTER_X, y + 190, 24, FONT_REGULAR, TH_TEXT_DIM, ALIGN_CENTER, w - 80,
                  app.storage_error);
    static const int glyphs[] = {GLYPH_SQUARE};
    static const char *const labels[] = {"Settings"};
    app_draw_hints(glyphs, labels, 1, NULL);
    if (pressed & BIT(BTN_SQUARE))
        app_open_settings(SCREEN_LIBRARY);
    app_draw_toast();
}

static void change_category(int step)
{
    int c = app.global.shelf_category;
    for (int i = 0; i < CAT_COUNT; ++i)
    {
        c = (c + step + CAT_COUNT) % CAT_COUNT;
        if (c == CAT_ALL || category_size(c) > 0)
            break;
    }
    if (c == app.global.shelf_category)
        return;
    int keep = selected_game();
    app.global.shelf_category = c;
    build_view(keep);
    S.title_fade = 0;
    sfx_play(SFX_CLICK);
}

/* ---------------------------------------------------------------- cover picker */

/* Choose a game's cover from the images in covers/ and in the game's folder
 * (GitHub issue #1). The pick is copied to covers/custom/<id>.<ext>, which
 * the cover lookup tries first; "Automatic" removes it. */
#define PICK_MAX 400
#define PICK_ROWS 11

static struct
{
    bool open;
    float t;
    int game;
    int count, cursor; /* entry 0 is "Automatic" */
    float scroll;
    char (*files)[PSXS5_PATH_MAX];
    bool unlisted; /* covers/ could not be listed (sandboxed) */
    PlatTexture *preview;
    int preview_for;
} P;

static bool pick_scan(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d)
        return false;
    struct dirent *e;
    while ((e = readdir(d)) && P.count < PICK_MAX)
        if (e->d_name[0] != '.' && covers_is_image(e->d_name))
            path_join(P.files[P.count++], PSXS5_PATH_MAX, dir, e->d_name);
    closedir(d);
    return true;
}

static int pick_compare(const void *a, const void *b)
{
    const char *x = strrchr((const char *)a, '/'), *y = strrchr((const char *)b, '/');
    return str_icmp(x ? x + 1 : (const char *)a, y ? y + 1 : (const char *)b);
}

static void picker_open(int game)
{
    if (!P.files && !(P.files = malloc(sizeof(*P.files) * (PICK_MAX + 1))))
        return;
    P.open = true;
    P.game = game;
    P.count = 1; /* "Automatic" */
    P.cursor = 0;
    P.scroll = 0;
    P.preview_for = -1;
    P.files[0][0] = '\0';
    pick_scan(app.library.games[game].folder);
    /* the covers PSXS5 downloaded for it (flat, 3D, by name) */
    const char *serial = app.library.games[game].serial;
    for (int k = 0; k < 3 && P.count < PICK_MAX; ++k)
    {
        if (k < 2 && !serial[0])
            continue;
        char file[140];
        if (k == 2)
            snprintf(file, sizeof(file), "boxart/%.100s.png", app.library.games[game].id);
        else
            snprintf(file, sizeof(file), k ? "3d/%s.png" : "default/%s.jpg", serial);
        path_join(P.files[P.count], PSXS5_PATH_MAX, app.paths.covers, file);
        FILE *f = fopen(P.files[P.count], "rb");
        if (f)
        {
            fclose(f);
            ++P.count;
        }
    }
    int from = P.count;
    P.unlisted = !pick_scan(app.paths.covers);
    qsort(P.files + from, (size_t)(P.count - from), sizeof(*P.files), pick_compare);
}

static void picker_close(void)
{
    P.open = false;
    plat_texture_free(P.preview);
    P.preview = NULL;
}

static void picker_choose(void)
{
    const Game *g = &app.library.games[P.game];
    char dir[PSXS5_PATH_MAX], path[PSXS5_PATH_MAX], name[96];
    covers_custom_dir(dir, sizeof(dir));
    static const char *const exts[] = {"png", "jpg", "jpeg"};
    for (int i = 0; i < 3; ++i) /* only the copies PSXS5 made itself */
    {
        snprintf(name, sizeof(name), "%s.%s", g->id, exts[i]);
        path_join(path, sizeof(path), dir, name);
        remove(path);
    }
    if (P.cursor > 0)
    {
        char ext[8];
        str_copy(ext, sizeof(ext), path_ext(P.files[P.cursor]));
        for (char *c = ext; *c; ++c)
            *c = (char)(*c | 0x20);
        snprintf(name, sizeof(name), "%s.%s", g->id, ext);
        path_join(path, sizeof(path), dir, name);
        if (!make_dirs(dir) || !file_copy(P.files[P.cursor], path))
        {
            app_toast("Could not save the cover");
            sfx_play(SFX_BACK);
            return;
        }
    }
    covers_reload(P.game);
    app_toast(P.cursor > 0 ? "Cover changed" : "Cover set to automatic");
    sfx_play(SFX_SELECT);
    picker_close();
}

static void picker_input(uint32_t pressed)
{
    int before = P.cursor;
    if (pressed & BIT(BTN_UP))
        --P.cursor;
    if (pressed & BIT(BTN_DOWN))
        ++P.cursor;
    if (pressed & (BIT(BTN_LEFT) | BIT(BTN_L1) | BIT(BTN_L2)))
        P.cursor -= PICK_ROWS;
    if (pressed & (BIT(BTN_RIGHT) | BIT(BTN_R1) | BIT(BTN_R2)))
        P.cursor += PICK_ROWS;
    P.cursor = P.cursor < 0 ? 0 : P.cursor >= P.count ? P.count - 1 : P.cursor;
    if (P.cursor != before)
        sfx_play(SFX_CLICK);
    if (pressed & BIT(BTN_CROSS))
        picker_choose();
    else if (pressed & (BIT(BTN_CIRCLE) | BIT(BTN_SQUARE)))
    {
        picker_close();
        sfx_play(SFX_BACK);
    }
}

static void picker_draw(void)
{
    P.t = fminf(fmaxf(P.t + (P.open ? app.dt : -app.dt) * 8.0f, 0.0f), 1.0f);
    if (P.t <= 0.0f || !P.files)
        return;
    float t = P.t, e = 1.0f - (1.0f - t) * (1.0f - t);
    draw_rect(0, 0, plat_width(), plat_height(), argb_alpha(0xc0000000u, t));
    const float w = 1400, h = 800, x = CENTER_X - w * 0.5f, y = 130 + (1.0f - e) * 40;
    draw_rrect(x, y, w, h, TH_RADIUS, argb_alpha(TH_CARD, t));
    text_draw(x + 40, y + 30, 32, FONT_BOLD, argb_alpha(TH_TEXT, t), ALIGN_LEFT, tr("Choose a cover"));
    text_draw_fit(x + 40, y + 76, 22, FONT_REGULAR, argb_alpha(TH_TEXT_DIM, t), ALIGN_LEFT, 760,
                  app.library.games[P.game].title);

    /* the list */
    const float lx = x + 40, ly = y + 130, lw = 760, rh = 50;
    float target = fminf((float)(P.cursor - PICK_ROWS / 2), (float)(P.count - PICK_ROWS));
    anim_approach(&P.scroll, fmaxf(target, 0.0f), app.dt, TH_SNAP);
    int first = (int)P.scroll;
    for (int i = first; i < P.count && i <= first + PICK_ROWS; ++i)
    {
        float ry = ly + (i - P.scroll) * rh;
        if (ry < ly - rh * 0.5f || ry > ly + rh * (PICK_ROWS - 0.5f))
            continue;
        bool on = i == P.cursor;
        if (on)
            draw_rrect(lx, ry, lw, rh - 6, TH_RADIUS_SMALL, argb_alpha(TH_ROW_SELECTED, t));
        const char *label = tr("Automatic (by serial or name)");
        int icon = ICON_REFRESH;
        if (i > 0)
        {
            const char *slash = strrchr(P.files[i], '/');
            label = slash ? slash + 1 : P.files[i];
            icon = strncmp(P.files[i], app.paths.covers, strlen(app.paths.covers)) == 0 ? ICON_CARDS
                                                                                        : ICON_FOLDER;
        }
        icon_draw(icon, lx + 14, ry + 8, 28, argb_alpha(on ? TH_FOCUS : TH_TEXT_DIM, t));
        text_draw_fit(lx + 56, ry + 10, 22, on ? FONT_BOLD : FONT_REGULAR,
                      argb_alpha(on ? TH_TEXT : TH_TEXT_SOFT, t), ALIGN_LEFT, lw - 72, label);
    }
    if (P.count == 1)
        text_draw_fit(lx, ly + rh * 1.5f, 22, FONT_REGULAR, argb_alpha(TH_TEXT_DIM, t), ALIGN_LEFT, lw,
                      tr(P.unlisted ? "Can't list the covers folder: unlock /data in Settings, System"
                                    : "No images yet: put .png or .jpg files in /data/GBAR5/covers/"));

    /* the preview, decoded once per selected file */
    const float px = x + 860, py = y + 130, pw = 500, ph = 560;
    draw_rrect(px, py, pw, ph, TH_RADIUS_SMALL, argb_alpha(TH_PILL, t));
    if (P.open && P.preview_for != P.cursor)
    {
        plat_texture_free(P.preview);
        P.preview = NULL;
        P.preview_for = P.cursor;
        int iw, ih;
        uint8_t *rgba = P.cursor > 0 ? covers_decode(P.files[P.cursor], &iw, &ih) : NULL;
        if (rgba)
        {
            P.preview = plat_texture_create(rgba, iw, ih, true);
            free(rgba);
        }
    }
    PlatTexture *shown = P.cursor > 0 ? P.preview : covers_get(P.game);
    if (shown)
    {
        int iw, ih;
        plat_texture_size(shown, &iw, &ih);
        float k = fminf((pw - 40) / iw, (ph - 40) / ih);
        plat_draw_texture(shown, px + (pw - iw * k) * 0.5f, py + (ph - ih * k) * 0.5f, iw * k, ih * k,
                          argb_alpha(0xffffffffu, t), true);
    }
    else if (P.cursor > 0)
        text_draw(px + pw * 0.5f, py + ph * 0.5f - 12, 22, FONT_REGULAR, argb_alpha(TH_TEXT_DIM, t),
                  ALIGN_CENTER, tr("Can't read this image"));
    text_draw_fit(x + 40, y + h - 56, 20, FONT_REGULAR, argb_alpha(TH_TEXT_DIM, t), ALIGN_LEFT, w - 80,
                  tr("Images from /data/GBAR5/covers/ and the game's folder. Named like the game, they are used without picking."));
}

/* ---------------------------------------------------------------- grid layout */

#define GRID_COLS 5
#define GRID_ROWS 3

/* Memory Card: covers in a grid on the left, the chosen game's card on the right */
static void draw_grid(int game, float launch)
{
    const float x0 = TH_MARGIN + 8, y0 = 170, tile = 212, gap = 22, title_h = 40;
    const float cell_h = tile + title_h + gap;
    /* scroll by rows, keeping the cursor's row in view */
    static float scroll;
    int row = S.cursor / GRID_COLS;
    static int top_row;
    if (row < top_row)
        top_row = row;
    if (row >= top_row + GRID_ROWS)
        top_row = row - GRID_ROWS + 1;
    anim_approach(&scroll, (float)top_row, app.dt, TH_SNAP);
    plat_set_clip(0, (int)y0 - 16, (int)(x0 + GRID_COLS * (tile + gap)), (int)(GRID_ROWS * cell_h + 8));
    int first = (int)floorf(scroll) * GRID_COLS, last = first + (GRID_ROWS + 1) * GRID_COLS;
    for (int k = first; k < last && k < S.view_count; ++k)
    {
        int c = k % GRID_COLS, r = k / GRID_COLS;
        float x = x0 + c * (tile + gap), y = y0 + (r - scroll) * cell_h;
        bool on = k == S.cursor;
        int index = S.view[k];
        const Game *g = &app.library.games[index];
        draw_rrect(x, y, tile, tile, TH_RADIUS_SMALL, TH_CARD);
        PlatTexture *t = covers_get(index);
        if (t)
        {
            int tw, th;
            plat_texture_size(t, &tw, &th);
            float k2 = fminf((tile - 20) / tw, (tile - 20) / th), w = tw * k2, h = th * k2;
            plat_draw_texture(t, x + (tile - w) * 0.5f, y + (tile - h) * 0.5f, w, h, 0xffffffffu, true);
        }
        else
            icon_draw(ICON_DISC, x + tile * 0.5f - 32, y + tile * 0.5f - 32, 64, TH_TEXT_DIM);
        draw_ach_badge(g, x + tile, y + tile, 15);
        if (on)
            draw_rrect_outline(x - 5, y - 5, tile + 10, tile + 10, TH_RADIUS_SMALL + 4, 4, theme.cover_outline);
        text_draw_fit(x + 4, y + tile + 8, 18, on ? FONT_BOLD : FONT_REGULAR, on ? TH_TEXT : TH_TEXT_DIM, ALIGN_LEFT,
                      tile - 8, g->title);
    }
    plat_set_clip(0, 0, 0, 0);

    /* the chosen game's card */
    const Game *g = &app.library.games[game];
    const float cx = x0 + GRID_COLS * (tile + gap) + 40, cw = plat_width() - TH_MARGIN - cx, cy = y0, ch = 760;
    draw_rrect(cx, cy, cw, ch, TH_RADIUS, TH_CARD);
    PlatTexture *t = covers_get(game);
    const float art = 340;
    if (t)
    {
        int tw, th;
        plat_texture_size(t, &tw, &th);
        float k2 = fminf((cw - 64) / tw, art / th), w = tw * k2, h = th * k2;
        plat_draw_texture(t, cx + (cw - w) * 0.5f, cy + 32, w, h, 0xffffffffu, true);
    }
    float ty = cy + 32 + art + 28;
    float a = 1.0f - launch;
    text_draw_fit(cx + 32, ty, 36, FONT_BOLD, argb_alpha(TH_TEXT, a), ALIGN_LEFT, cw - 64, g->title);
    GameStats *st = stats_get(g->id);
    char played[48] = "", ach[48] = "";
    if (st && st->seconds)
        stats_format_time(st->seconds, played, sizeof(played));
    if (st && st->ach_total > 0 && st->ach_unlocked >= 0)
        snprintf(ach, sizeof(ach), "%d / %d", st->ach_unlocked, st->ach_total);
    const char *labels[4] = {"Serial", "Region", "Played", "Achievements"};
    const char *values[4] = {g->serial[0] ? g->serial : tr("Unknown"), shelf_region_name(g->serial),
                             played[0] ? played : tr("Not yet"), ach[0] ? ach : tr("Unknown")};
    for (int i = 0; i < 4; ++i)
    {
        float ry = ty + 64 + i * 46;
        text_draw(cx + 32, ry, 22, FONT_REGULAR, argb_alpha(TH_TEXT_DIM, a), ALIGN_LEFT, tr(labels[i]));
        text_draw_fit(cx + cw * 0.45f, ry, 22, FONT_BOLD, argb_alpha(TH_TEXT, a), ALIGN_LEFT, cw * 0.55f - 32, values[i]);
    }
    if (st && st->favorite)
        icon_draw(ICON_STAR, cx + cw - 64, cy + ch - 64, 36, TH_GOLD);
}

/* ---------------------------------------------------------------- spines layout */

/* Record Shelf: each game a spine (a slice of its cover's art), the chosen one
 * pulled out face-on above the shelf */
static void draw_spines(float launch)
{
    const float spine_w = 52, gap = 8, spine_h = 330, base = CENTER_Y + COVER_H * 0.5f + 8;
    const float face_h = COVER_H * 0.9f;
    PlatTexture *sel = covers_get(S.view[S.cursor]);
    float face_w = face_h * 0.88f;
    if (sel)
    {
        int tw, th;
        plat_texture_size(sel, &tw, &th);
        face_w = face_h * tw / th;
    }
    /* positions relative to the selection, which slides (S.pos) */
    for (int side = -1; side <= 1; side += 2)
        for (int d = 1; d <= 14; ++d)
        {
            int k = S.cursor + d * side;
            if (k < 0 || k >= S.view_count)
                continue;
            float off = (k - S.pos);
            float x = CENTER_X + side * (face_w * 0.5f + 24) + (off - side) * (spine_w + gap) - (side < 0 ? spine_w : 0);
            if (x + spine_w < 0 || x > plat_width())
                continue;
            int index = S.view[k];
            PlatTexture *t = covers_get(index);
            float y = base - spine_h;
            uint32_t shade = d > 8 ? 0xff9a9a9au : 0xffd8d8d8u;
            if (t)
            {
                /* a slice from the middle of the art, as a spine */
                int tw, th;
                plat_texture_size(t, &tw, &th);
                int sw = tw / 9 > 1 ? tw / 9 : 1;
                plat_draw_texture_region(t, tw / 2 - sw / 2, 0, sw, th, x, y, spine_w, spine_h, shade);
            }
            else
                draw_rrect(x, y, spine_w, spine_h, 3, TH_CARD);
            draw_rect(x + spine_w - 3, y, 3, spine_h, 0x40000000u); /* the spine's edge */
        }
    /* the chosen game, face-on and lifted */
    float lift = 18.0f + launch * 20.0f;
    float fx = CENTER_X - face_w * 0.5f, fy = base - face_h - lift;
    draw_rrect(fx + 10, fy + 24, face_w, face_h, 8, 0x70000000u); /* shadow */
    if (sel)
        plat_draw_texture(sel, fx, fy, face_w, face_h, 0xffffffffu, true);
    else
        draw_rrect(fx, fy, face_w, face_h, 8, TH_CARD);
    draw_rrect_outline(fx - 5, fy - 5, face_w + 10, face_h + 10, 10, 3, theme.cover_outline);
    draw_ach_badge(&app.library.games[S.view[S.cursor]], fx + face_w, fy + face_h, 20);
}

/* ---------------------------------------------------------------- list layout */

/* Classic / Neon: the games as a list down the left, the chosen game's box
 * beside it, and the game's own gameplay picture filling the screen behind
 * (the picture the idle slideshow would show). Up / Down walk the list,
 * Left / Right jump between letters. */

#define LIST_X TH_MARGIN
#define LIST_Y 150.0f
#define LIST_W 560.0f
#define LIST_ROW 66.0f
#define LIST_ROWS 12

/* The list layout's backdrop is already drawn: shelf_screen puts the selected
 * game's screenshot behind every layout, once per frame (draw_shelf_picture). */
static void draw_list_backdrop(int game)
{
    (void)game;
}

static void draw_list(int game, float launch)
{
    const float sw = (float)plat_width();
    draw_list_backdrop(game);

    /* ------------------------------------------------ the list */
    static int top_row;
    static float scroll;
    top_row = list_top_row(top_row, S.cursor, LIST_ROWS, S.view_count);
    anim_approach(&scroll, (float)top_row, app.dt, TH_SNAP);

    const float px = LIST_X - 10, py = LIST_Y - 14;
    const float pw = LIST_W + 20, ph = LIST_ROWS * LIST_ROW + 28;
    draw_rrect(px, py, pw, ph, 22, TH_BG_A(0xd8));
    draw_rrect_outline(px, py, pw, ph, 22, 2, TH_CARD_A(0x50));

    plat_set_clip((int)px, (int)py, (int)pw, (int)ph);
    for (int k = top_row - 1; k <= top_row + LIST_ROWS; ++k)
    {
        if (k < 0 || k >= S.view_count)
            continue;
        int index = S.view[k];
        const Game *g = &app.library.games[index];
        float y = LIST_Y + (k - scroll) * LIST_ROW;
        bool on = k == S.cursor;
        if (on)
        {
            draw_rrect(LIST_X, y + 4, LIST_W, LIST_ROW - 8, 14, TH_ROW_SELECTED);
            draw_rect(LIST_X + 2, y + 12, 5, LIST_ROW - 24, TH_FOCUS);
        }
        /* one letter where each group starts: A, B, C... */
        char initial = title_initial(g->title);
        if (k == 0 || title_initial(app.library.games[S.view[k - 1]].title) != initial)
        {
            char letter[2] = {initial, 0};
            text_draw(LIST_X + 18, y + (LIST_ROW - 22) * 0.5f, 22, FONT_BOLD, on ? TH_FOCUS : TH_HINT,
                      ALIGN_LEFT, letter);
        }

        /* the small cover */
        PlatTexture *t = covers_get(index);
        const float th = 52, tw = 38;
        float tx = LIST_X + 52, ty = y + (LIST_ROW - th) * 0.5f;
        draw_rrect(tx, ty, tw, th, 5, TH_CARD);
        if (t)
        {
            int iw, ih;
            plat_texture_size(t, &iw, &ih);
            float f = fminf(tw / iw, th / ih), dw = iw * f, dh = ih * f;
            plat_draw_texture(t, tx + (tw - dw) * 0.5f, ty + (th - dh) * 0.5f, dw, dh, 0xffffffffu, true);
        }
        text_draw_fit(LIST_X + 104, y + 10, on ? 24 : 23, on ? FONT_BOLD : FONT_REGULAR,
                      on ? TH_TEXT : TH_TEXT_SOFT, ALIGN_LEFT, LIST_W - 120, g->title);
        text_draw_fit(LIST_X + 104, y + 39, 17, FONT_REGULAR, on ? TH_TEXT_DIM : TH_HINT, ALIGN_LEFT,
                      LIST_W - 120, shelf_region_name(g->serial));
    }
    plat_set_clip(0, 0, 0, 0);

    if (game < 0)
        return;

    /* ------------------------------------------------ the box, beside the list */
    const Game *g = &app.library.games[game];
    const float art_h = 580, art_y = 160;
    PlatTexture *cover = covers_get(game);
    float aspect = 0.88f;
    if (cover)
    {
        int iw, ih;
        plat_texture_size(cover, &iw, &ih);
        aspect = (float)iw / ih;
    }
    float art_w = art_h * aspect, art_x = LIST_X + LIST_W + 76;
    draw_glow(art_x - 10, art_y - 10, art_w + 20, art_h + 20, 28, (TH_FOCUS & 0xffffffu) | 0x40000000u);
    draw_rrect(art_x + 10, art_y + 18, art_w, art_h, 10, 0x60000000u); /* shadow */
    if (cover)
        plat_draw_texture(cover, art_x, art_y, art_w, art_h, 0xffffffffu, true);
    else
        draw_rrect(art_x, art_y, art_w, art_h, 10, TH_CARD);
    draw_rrect_outline(art_x - 5, art_y - 5, art_w + 10, art_h + 10, 14, 4, theme.cover_outline);
    draw_ach_badge(g, art_x + art_w, art_y + art_h, 20);

    /* ------------------------------------------------ its title and facts */
    float a = 1.0f - launch;
    float ix = art_x + art_w + 52, iw = sw - TH_MARGIN - ix, ty = art_y + 6;
    if (iw < 260)
        return; /* very wide cover: the facts would not fit */
    GameStats *st = stats_get(g->id);
    if (st && st->favorite)
        icon_draw(ICON_STAR, ix + iw - 34, ty + 2, 34, argb_alpha(TH_GOLD, a));
    text_draw_fit(ix, ty, 40, FONT_BOLD, argb_alpha(TH_TEXT, a), ALIGN_LEFT, iw - (st && st->favorite ? 46 : 0),
                  g->title);

    /* pills: region, serial, discs, played */
    char discs[32], played[48] = "", played_tag[64] = "";
    snprintf(discs, sizeof(discs), "%s", tr("GBA cartridge"));
    if (st)
    {
        stats_format_time(st->seconds, played, sizeof(played));
        if (played[0])
            snprintf(played_tag, sizeof(played_tag), tr("Played %s"), played);
    }
    const char *tags[4] = {shelf_region_name(g->serial), g->serial[0] ? g->serial : tr("No serial"), discs,
                           played_tag};
    float tx2 = ix, ty2 = ty + 56;
    for (int i = 0; i < 4; ++i)
    {
        if (!tags[i][0])
            continue;
        float w = text_width(19, FONT_REGULAR, tags[i]) + 36 * 0.9f;
        if (tx2 + w > ix + iw)
            break; /* no room for another pill */
        draw_pill(tx2, ty2, 36, 19, argb_alpha(TH_PILL_A(0xc0), a), argb_alpha(TH_TEXT_SOFT, a), tags[i]);
        tx2 += w + 8;
    }

    /* what the game databases and the stats know */
    const GameInfo *info = game_info(g);
    char released[96] = "", genre[96] = "", players[48] = "", when[48] = "";
    if (info && info->released[0])
        str_copy(released, sizeof(released), info->released);
    else if (info && info->year)
        snprintf(released, sizeof(released), "%d", info->year);
    if (info && info->genre[0])
        str_copy(genre, sizeof(genre), info->genre);
    if (info && info->max_players > 1)
        snprintf(players, sizeof(players), tr("1 to %d"), info->max_players);
    else if (info && info->max_players == 1)
        str_copy(players, sizeof(players), "1");
    if (st)
        stats_format_when(st->last_played, when, sizeof(when));
    const char *labels[4] = {"Released", "Genre", "Players", "Last played"};
    const char *values[4] = {released[0] ? released : tr("Unknown"), genre[0] ? genre : tr("Unknown"),
                             players[0] ? players : tr("Unknown"), when[0] ? when : tr("Not yet")};
    for (int i = 0; i < 4; ++i)
    {
        float ry = ty + 126 + i * 44;
        text_draw(ix, ry, 19, FONT_REGULAR, argb_alpha(TH_TEXT_DIM, a), ALIGN_LEFT, tr(labels[i]));
        text_draw_fit(ix + 180, ry, 19, FONT_BOLD, argb_alpha(TH_TEXT, a), ALIGN_LEFT, iw - 180, values[i]);
    }
    if (st && st->ach_unlocked >= 0 && st->ach_total > 0)
        draw_ach_row(ix, ix + iw, ty + 126 + 4 * 44 + 24, 19, st->ach_unlocked, st->ach_total, a);
}

/* ---------------------------------------------------------------- the cartridge tray */

/* GBAR5's shelf. The chosen game sits on a 3:2 screen -- the GBA's own 240x160
 * shape, scaled up -- on the left, and the library runs down the right as a
 * column of cartridges. Deliberately not a cover flow: nothing fans out, no
 * jewel cases, no discs; your eye starts at the screen. */
static void draw_cart(int game, float launch)
{
    const float sw = plat_width();
    const float px = 96, py = 168, pw = 776, ph = 780;
    const float sx = px + 40, sy = py + 40, scw = pw - 80, sch = scw * 2.0f / 3.0f; /* 3:2, the GBA's shape */

    draw_rrect(px, py, pw, ph, 26, TH_CARD_A(0xf0));
    draw_rrect_outline(px, py, pw, ph, 26, 2, TH_CARD_A(0x60));

    /* the screen: a bezel and the glass, no hardware logos on it */
    draw_rrect(sx - 12, sy - 12, scw + 24, sch + 24, 18, TH_BG_DEEP);
    draw_rrect(sx, sy, scw, sch, 8, theme.light ? 0xffffffffu : 0xff08150eu);
    if (game >= 0)
    {
        const Game *g = &app.library.games[game];
        /* What the game looks like: its in-game screenshot fills the screen,
         * which is exactly the GBA's 3:2 shape. Settings > Shelf > Card image
         * can put the cartridge's cover art here instead; the background
         * behind the shelf always keeps the in-game picture. Either picture
         * falls back to the other, and the game's name is the last one. */
        bool cover_card = app.global.shelf_card == SHELF_CARD_COVER;
        PlatTexture *pic = cover_card ? covers_get(game) : art_get(g, ART_SNAP);
        if (!pic)
            pic = cover_card ? art_get(g, ART_SNAP) : covers_get(game);
        if (pic)
        {
            int iw, ih;
            plat_texture_size(pic, &iw, &ih);
            float f = fminf(scw / iw, sch / ih), dw = iw * f, dh = ih * f;
            float dx = sx + (scw - dw) * 0.5f, dy = sy + (sch - dh) * 0.5f;
            if (cover_card)
            {
                /* a cover is taller than the GBA's screen: the empty sides read
                 * as a frame, so the art gets a shadow and an outline */
                draw_rrect(dx + 8, dy + 12, dw, dh, 8, 0x70000000u);
                plat_draw_texture(pic, dx, dy, dw, dh, argb_alpha(0xffffffffu, 1.0f - launch), true);
                draw_rrect_outline(dx - 4, dy - 4, dw + 8, dh + 8, 10, 3, theme.cover_outline);
            }
            else
                plat_draw_texture(pic, dx, dy, dw, dh, argb_alpha(0xffffffffu, 1.0f - launch), true);
        }
        else
            text_draw_fit(sx + scw * 0.5f, sy + sch * 0.5f - 20, 30, FONT_BOLD, TH_TEXT_SOFT, ALIGN_CENTER,
                          scw - 60, g->title);
    }

    if (game < 0)
        return;
    const Game *g = &app.library.games[game];
    GameStats *st = stats_get(g->id);
    float a = 1.0f - launch;

    /* title, with the favourite star */
    float ty = sy + sch + 22;
    if (st && st->favorite)
    {
        float tw = fminf(text_width(38, FONT_BOLD, g->title), scw - 56);
        icon_draw(ICON_STAR, sx + tw + 14, ty + 6, 30, argb_alpha(TH_GOLD, a));
    }
    text_draw_fit(sx, ty, 38, FONT_BOLD, argb_alpha(TH_TEXT, a), ALIGN_LEFT, scw, g->title);

    /* region, cartridge, played */
    char played[48] = "", played_tag[64] = "";
    if (st)
    {
        stats_format_time(st->seconds, played, sizeof(played));
        if (played[0])
            snprintf(played_tag, sizeof(played_tag), tr("Played %s"), played);
    }
    const char *tags[3] = {shelf_region_name(g->serial), tr("GBA cartridge"), played_tag};
    float tx = sx, ty2 = ty + 50;
    for (int i = 0; i < 3; ++i)
        if (tags[i][0])
            tx += draw_pill(tx, ty2, 36, 20, argb_alpha(TH_PILL_A(0xc0), a), argb_alpha(TH_TEXT_SOFT, a),
                            tags[i]) +
                  10;

    /* what the shelf knows about it: the release, the genre, the last time */
    {
        const GameInfo *info = game_info(g);
        char released[96] = "", genre[96] = "", when[48] = "";
        if (info && info->released[0])
            str_copy(released, sizeof(released), info->released); /* the cartridge's day, e.g. 2001-11-11 */
        else if (info && info->year)
            snprintf(released, sizeof(released), info->developer[0] ? "%d  \xc2\xb7  %s" : "%d", info->year,
                     info->developer);
        if (info && info->genre[0])
            str_copy(genre, sizeof(genre), info->genre);
        if (st)
            stats_format_when(st->last_played, when, sizeof(when));
        const char *labels[3] = {"Released", "Genre", "Last played"};
        const char *values[3] = {released[0] ? released : tr("Unknown"),
                                 genre[0] ? genre : tr("Unknown"), when[0] ? when : tr("Not yet")};
        float fy = ty2 + 50;
        for (int i = 0; i < 3; ++i)
        {
            text_draw(sx, fy + i * 34, 20, FONT_REGULAR, argb_alpha(TH_TEXT_DIM, a), ALIGN_LEFT, tr(labels[i]));
            text_draw_fit(sx + 150, fy + i * 34, 20, FONT_BOLD, argb_alpha(TH_TEXT_SOFT, a), ALIGN_LEFT,
                          scw - 150, values[i]);
        }
    }

    /* achievements, when the game has any: the card's own footer line */
    if (st && st->ach_unlocked >= 0 && st->ach_total > 0)
        draw_ach_row(sx, sx + scw, ty2 + 164, 20, st->ach_unlocked, st->ach_total, a);

    /* the column of games */
    const float lx = 940, lw = sw - TH_MARGIN - lx, ly = py, row_h = 88;
    const int rows = 8;
    draw_rrect(lx, ly, lw, rows * row_h + 28, 26, TH_CARD_A(0xd8));
    draw_rrect_outline(lx, ly, lw, rows * row_h + 28, 26, 2, TH_CARD_A(0x50));
    static int top_row;
    static float scroll;
    top_row = list_top_row(top_row, S.cursor, rows, S.view_count);
    anim_approach(&scroll, (float)top_row, app.dt, 18.0f);

    plat_set_clip((int)lx, (int)ly, (int)lw, (int)(rows * row_h + 28));
    for (int k = top_row; k < top_row + rows && k < S.view_count; ++k)
    {
        const Game *row_game = &app.library.games[S.view[k]];
        float ry = ly + 14 + (k - scroll) * row_h;
        bool on = k == S.cursor;
        if (on)
        {
            draw_rrect(lx + 12, ry + 4, lw - 24, row_h - 12, 14, TH_ROW_SELECTED);
            draw_rrect(lx + 12, ry + 16, 5, row_h - 36, 3, TH_FOCUS);
        }
        /* one letter where each group starts: A, B, C... */
        char initial = title_initial(row_game->title);
        if (k == 0 || title_initial(app.library.games[S.view[k - 1]].title) != initial)
        {
            char letter[2] = {initial, 0};
            text_draw(lx + 20, ry + (row_h - 18) * 0.5f, 18, FONT_BOLD, on ? TH_FOCUS : TH_HINT, ALIGN_LEFT,
                      letter);
        }

        /* the cartridge chip: the same shell as the shelf, small */
        PlatTexture *t = covers_get(S.view[k]);
        const float ch = 52, cw = ch * 0.70f, cx = lx + 40, cy = ry + (row_h - ch) * 0.5f;
        draw_rrect(cx, cy, cw, ch, 5, theme.light ? 0xffcfe3d7u : 0xff1d5038u);
        if (t)
        {
            int iw, ih;
            plat_texture_size(t, &iw, &ih);
            float f2 = fminf((cw - 6) / iw, (ch * 0.68f) / ih), dw = iw * f2, dh = ih * f2;
            plat_draw_texture(t, cx + (cw - dw) * 0.5f, cy + 3 + (ch * 0.68f - dh) * 0.5f, dw, dh,
                              0xffffffffu, true);
        }
        text_draw_fit(cx + cw + 26, ry + 22, on ? 27 : 26, on ? FONT_BOLD : FONT_REGULAR,
                      on ? TH_TEXT : TH_TEXT_SOFT, ALIGN_LEFT, lw - (cw + 300), row_game->title);
        char played2[48] = "", meta[128];
        GameStats *rst = stats_get(row_game->id);
        if (rst)
            stats_format_time(rst->seconds, played2, sizeof(played2));
        if (played2[0])
            snprintf(meta, sizeof(meta), "%s  \xc2\xb7  %s", shelf_region_name(row_game->serial), played2);
        else
            str_copy(meta, sizeof(meta), shelf_region_name(row_game->serial));
        text_draw(lx + lw - 34, ry + 30, 20, FONT_REGULAR, on ? TH_TEXT_SOFT : TH_TEXT_DIM, ALIGN_RIGHT, meta);
    }
    plat_set_clip(0, 0, 0, 0);

    /* a scroll bar, so a long library reads as one list */
    if (S.view_count > rows)
    {
        float track = ly + 20, track_h = rows * row_h - 24;
        float thumb_h = track_h * (float)rows / S.view_count;
        float thumb_y = track + (track_h - thumb_h) * (float)top_row / (float)(S.view_count - rows);
        draw_rrect(lx + lw - 9, track, 4, track_h, 2, argb_alpha(TH_PILL, 0.7f));
        draw_rrect(lx + lw - 9, thumb_y, 4, thumb_h, 2, argb_alpha(TH_FOCUS, 0.8f));
    }
}



/* A GBA game is a cartridge. There is no disc, no case and no spin-up: a game
 * starts with a short fade and nothing else. (The old PS1 disc animation is
 * gone from this build on purpose -- this app is for Game Boy Advance games.) */

/* ---------------------------------------------------------------- idle */

/* Nothing pops up while the shelf is left alone. The old idle slideshow used
 * to fill the screen with a downloaded Game Boy Advance screenshot; it is gone. */
static bool slideshow(uint32_t pressed)
{
    (void)pressed;
    return false;
}

void shelf_screen(uint32_t pressed)
{
    if (app.storage_error[0])
    {
        storage_screen(pressed);
        return;
    }
    if (slideshow(pressed))
        return;

    /* ------------------------------------------------ input */
    if (P.open)
    {
        picker_input(pressed);
        pressed = 0;
    }
    if (S.dialog)
    {
        if (pressed & (BIT(BTN_LEFT) | BIT(BTN_RIGHT) | BIT(BTN_UP) | BIT(BTN_DOWN)))
        {
            S.dialog_choice ^= 1;
            sfx_play(SFX_CLICK);
        }
        if (pressed & BIT(BTN_CROSS))
        {
            S.dialog = false;
            S.resume = S.dialog_choice == 0;
            S.launch_t = 0.0001f;
            sfx_play(SFX_SELECT);
        }
        if (pressed & BIT(BTN_CIRCLE))
        {
            S.dialog = false;
            sfx_play(SFX_BACK);
        }
        pressed = 0;
    }
    if (S.launch_t <= 0.0f)
    {
        int before = S.cursor;
        /* Up / Down walk the shelf one game at a time (the repeat is ours, so
         * it behaves the same on every pad). Left / Right walk the alphabet
         * instead: left is forward (A, B, C... to Z), right comes back again
         * (Z, Y, X... to A), stopping on each letter's first game. L2/R2 jump ten. */
        static int held_dir, held_steps;
        static uint64_t held_at;
        int dir = 0;
        if (pressed & BIT(BTN_UP))
            dir = -1;
        if (pressed & BIT(BTN_DOWN))
            dir = 1;
        uint64_t now = plat_ticks_us();
        if (dir)
        {
            S.cursor += dir;
            held_dir = dir;
            held_steps = 1;
            held_at = now;
        }
        else
        {
            int down = 0;
            for (int i = 0; i < PSXS5_MAX_PADS; ++i)
            {
                uint32_t b = app.pads[i].buttons;
                if (b & BIT(BTN_UP))
                    down = -1;
                if (b & BIT(BTN_DOWN))
                    down = 1;
            }
            if (!down)
                held_dir = held_steps = 0;
            else if (down == held_dir && held_steps > 0)
            {
                uint64_t waited = now - held_at;
                uint64_t first = 320000;                                    /* hold before repeating */
                uint64_t gap = held_steps > 30 ? 45000 : held_steps > 12 ? 60000 : 90000;
                if (waited > first && (waited - first) / gap >= (uint64_t)held_steps)
                {
                    S.cursor += down;
                    ++held_steps;
                }
            }
        }
        if (pressed & (BIT(BTN_LEFT) | BIT(BTN_RIGHT)))
        {
            /* the 150 ms gate keeps a held button at a pace you can stop on */
            if (now - letter_at >= 150000)
            {
                letter_at = now;
                /* Right = forward A->Z, Left = back Z->A */
                alphabet_jump((pressed & BIT(BTN_LEFT)) ? -1 : 1);
            }
        }
        else
            letter_at = 0; /* the next press jumps at once */
        if (pressed & BIT(BTN_L2))
            S.cursor -= 10;
        if (pressed & BIT(BTN_R2))
            S.cursor += 10;
        S.cursor = S.cursor < 0 ? 0 : S.cursor >= S.view_count ? (S.view_count ? S.view_count - 1 : 0)
                                                                : S.cursor;
        if (S.cursor != before)
        {
            sfx_play(SFX_CLICK);
            S.title_fade = 0.35f;
        }
        if (pressed & BIT(BTN_L1))
            change_category(-1);
        if (pressed & BIT(BTN_R1))
            change_category(1);
        if (pressed & BIT(BTN_START))
        {
            int keep = selected_game();
            app.global.sort_mode = (app.global.sort_mode + 1) % SORT_COUNT;
            build_view(keep);
            char msg[96];
            snprintf(msg, sizeof(msg), tr("Sorted by %s"), shelf_sort_name(app.global.sort_mode));
            app_toast(msg);
            sfx_play(SFX_CLICK);
        }
        int game = selected_game();
        if ((pressed & BIT(BTN_R3)) && game >= 0)
        {
            GameStats *st = stats_get(app.library.games[game].id);
            if (st)
            {
                st->favorite = !st->favorite;
                stats_save();
                app_toast(st->favorite ? "Added to favorites" : "Removed from favorites");
                sfx_play(SFX_SELECT);
                if (app.global.shelf_category == CAT_FAVORITES)
                    build_view(game);
            }
        }
        if ((pressed & BIT(BTN_L3)) && S.details && game >= 0)
        {
            /* hide / show again: the cursor stays where the game was */
            GameStats *st = stats_get(app.library.games[game].id);
            if (st)
            {
                st->hidden = !st->hidden;
                stats_save();
                app_toast(st->hidden ? "Hidden: find it under Hidden" : "Back on the shelf");
                sfx_play(SFX_SELECT);
                S.details = false;
                int keep_pos = S.cursor;
                build_view(-1);
                S.cursor = keep_pos < S.view_count ? keep_pos : (S.view_count ? S.view_count - 1 : 0);
                S.pos = (float)S.cursor;
                game = selected_game();
            }
        }
        if ((pressed & BIT(BTN_MENU)) && S.view_count > 1 && !S.details)
        {
            /* Surprise me: another game of this category; the shelf glides there */
            int pick = S.cursor;
            while (pick == S.cursor)
                pick = rand() % S.view_count;
            S.cursor = pick;
            S.title_fade = 0.35f;
            app_toast("Surprise!");
            sfx_play(SFX_SELECT);
            game = selected_game();
        }
        if (pressed & BIT(BTN_TRIANGLE) && game >= 0)
        {
            S.details = !S.details;
            sfx_play(S.details ? SFX_SELECT : SFX_BACK);
        }
        if ((pressed & BIT(BTN_CIRCLE)) && S.details)
        {
            S.details = false;
            sfx_play(SFX_BACK);
        }
        if ((pressed & BIT(BTN_CROSS)) && game >= 0)
        {
            const Game *g = &app.library.games[game];
            S.details = false;
            if (app.global.quick_resume && !ra_hardcore() && play_has_resume(g, &S.resume_age))
            {
                /* offer to continue */
                char path[PSXS5_PATH_MAX];
                static uint8_t rgba[THUMB_W * THUMB_H * 4];
                play_resume_path(g, path, sizeof(path));
                plat_texture_free(S.resume_thumb);
                S.resume_thumb = play_load_thumb(path, rgba)
                                     ? plat_texture_create(rgba, THUMB_W, THUMB_H, true)
                                     : NULL;
                S.dialog = true;
                S.dialog_choice = 0;
            }
            else
            {
                S.resume = false;
                S.launch_t = 0.0001f;
            }
            sfx_play(SFX_SELECT);
        }
        if ((pressed & BIT(BTN_SQUARE)) && S.details && game >= 0)
        {
            sfx_play(SFX_SELECT);
            picker_open(game);
        }
        else if (pressed & BIT(BTN_SQUARE))
        {
            sfx_play(SFX_SELECT);
            config_save(&app.global, app.paths.config);
            app_open_settings(SCREEN_LIBRARY);
        }
    }

    /* ------------------------------------------------ animation */
    anim_approach(&S.pos, (float)S.cursor, app.dt, 16.0f);
    S.details_t = fminf(fmaxf(S.details_t + (S.details ? app.dt : -app.dt) * 8.0f, 0.0f), 1.0f);
    if (S.title_fade > 0)
        S.title_fade = fmaxf(S.title_fade - app.dt, 0.0f);
    int game = selected_game();
    if (game >= 0)
        S.last_cursor_game = game;
    if (S.starting)
    {
        /* the last frame (disc and "Loading") stays on screen while the game loads */
        S.starting = false;
        S.launch_t = 0.0f;
        config_save(&app.global, app.paths.config);
        app_start_game(game, S.resume);
    }
    else if (S.launch_t > 0.0f)
    {
        S.launch_t = fminf(S.launch_t + app.dt, launch_total());
        if (S.launch_t >= launch_total())
            S.starting = true;
    }
    plat_profile("start");
    covers_update_view(S.view, S.view_count, S.cursor);
    update_tint(game);
    plat_profile("covers-upload");

    /* ------------------------------------------------ shelf */
    shelf_backdrop();
    /* the selected game's own picture, filling the screen behind the shelf */
    draw_shelf_picture(game);
    if (theme.shelf_plank)
    {
        const float py = CENTER_Y + COVER_H * 0.5f + 8;
        draw_rect(180, py + 30, plat_width() - 360, 22, 0x60000000u);  /* its shadow */
        draw_rrect(160, py, plat_width() - 320, 34, 4, 0xff7a4e2eu);
        draw_rect(160, py + 22, plat_width() - 320, 12, 0xff5e3a22u);
    }
    plat_profile("backdrop");
    /* the fade to black is the last LAUNCH_TIME; the disc animation comes first */
    float launch = S.launch_t > 0.0f ? fmaxf(S.launch_t - (launch_total() - LAUNCH_TIME), 0.0f) / LAUNCH_TIME : 0.0f;
    if (S.view_count > 0 && theme.layout == LAYOUT_CART)
    {
        draw_cart(game, launch);
        plat_profile("covers");
        draw_details(&app.library.games[game], S.details_t);
        plat_profile("info");
    }
    else if (S.view_count > 0 && theme.layout == LAYOUT_GRID)
    {
        draw_grid(game, launch);
        draw_details(&app.library.games[game], S.details_t);
        plat_profile("covers");
    }
    else if (S.view_count > 0 && theme.layout == LAYOUT_SPINES)
    {
        draw_spines(launch);
        plat_profile("covers");
        float text_a = (1.0f - launch) * (1.0f - S.title_fade / 0.35f * 0.8f);
        draw_info(&app.library.games[game], text_a);
        draw_details(&app.library.games[game], S.details_t);
        plat_profile("info");
    }
    else if (S.view_count > 0 && theme.layout == LAYOUT_LIST)
    {
        draw_list(game, launch);
        plat_profile("covers");
        draw_details(&app.library.games[game], S.details_t);
        plat_profile("info");
    }
    else if (S.view_count > 0)
    {
        int first = (int)floorf(S.pos) - 6, last = (int)ceilf(S.pos) + 6;
        /* far to near, so the selected cover is drawn last */
        for (int ring = 6; ring >= 0; --ring)
            for (int side = -1; side <= 1; side += 2)
            {
                int k = (int)floorf(S.pos + 0.5f) + ring * side;
                if (ring == 0 && side == 1)
                    continue;
                if (k < first || k > last || k < 0 || k >= S.view_count)
                    continue;
                float d = k - S.pos, ad = fabsf(d), near = ad < 1.0f ? ad : 1.0f;
                float ox = ad < 1.0f ? d * SIDE_GAP
                                     : (d > 0 ? 1.0f : -1.0f) * (SIDE_GAP + (ad - 1.0f) * STACK_GAP);
                float scale = 1.0f - near * (1.0f - SIDE_SCALE);
                if (ad > 1.0f) /* further down the shelf, each one a little smaller */
                    scale *= 1.0f / (1.0f + (ad - 1.0f) * 0.10f);
                float squeeze = 1.0f - near * (1.0f - SIDE_SQUEEZE);
                bool selected = k == S.cursor;
                if (selected)
                    scale *= 1.0f + launch * 0.12f;
                float fade = ad > 4.0f ? fmaxf(0.0f, 1.0f - (ad - 4.0f) / 2.0f) : 1.0f;
                float shade = (1.0f - near * 0.4f) * fade * (selected ? 1.0f : 1.0f - launch);
                uint8_t s8 = (uint8_t)(255 * shade);
                uint32_t tint = 0xff000000u | (uint32_t)s8 << 16 | (uint32_t)s8 << 8 | s8;
                int index = S.view[k];
                draw_cover(covers_get(index), &app.library.games[index], CENTER_X + ox, CENTER_Y,
                           COVER_H * scale, squeeze, tint, selected && ad < 0.25f && launch < 0.5f);
            }
        plat_profile("covers");
        float text_a = (1.0f - launch) * (1.0f - S.title_fade / 0.35f * 0.8f);
        draw_info(&app.library.games[game], text_a);
        draw_details(&app.library.games[game], S.details_t);
        plat_profile("info");
    }
    else
    {
        float w = 1200, h = 330, x = CENTER_X - w * 0.5f, y = 300;
        draw_rrect(x, y, w, h, TH_RADIUS, TH_CARD_SOFT);
        icon_draw(ICON_CARDS, CENTER_X - 32, y + 30, 64, TH_FOCUS);
        text_draw(CENTER_X, y + 108, 36, FONT_BOLD, TH_TEXT, ALIGN_CENTER, tr("Your shelf is empty"));
        char line[PSXS5_PATH_MAX + 64];
        snprintf(line, sizeof(line), tr("Games: %s/<Game name>/"), app.paths.games);
        text_draw(CENTER_X, y + 166, 24, FONT_REGULAR, TH_TEXT, ALIGN_CENTER, line);
        snprintf(line, sizeof(line), "ROMs: %s", app.paths.games);
        text_draw(CENTER_X, y + 206, 24, FONT_REGULAR, TH_TEXT, ALIGN_CENTER, line);
        if (app.sandboxed)
        {
            /* games copied another way can't be found without listing /data */
            text_draw(CENTER_X, y + 252, 22, FONT_REGULAR, TH_GOLD, ALIGN_CENTER,
                      tr("Nothing unlocked /data (LegacyJB, etaHEN...), so GBAR5 can't look into the games folder."));
            text_draw(CENTER_X, y + 286, 22, FONT_REGULAR, TH_TEXT_DIM, ALIGN_CENTER,
                      tr("On your PC:  python tools/gbar5_sync.py index --host <PS5 IP>"));
        }
        else
            text_draw(CENTER_X, y + 262, 22, FONT_REGULAR, TH_TEXT_DIM, ALIGN_CENTER,
                      tr("On your PC:  python tools/gbar5_sync.py upload --host <PS5 IP>"));
    }

    draw_header();
    plat_profile("header");

    /* Warm the pictures around the selection, after this frame has asked for
     * the one on screen: moving down the list lands on a picture already
     * decoded instead of an empty screen while it downloads. */
    static int warmed_cursor = -1;
    if (S.view_count > 0 && S.cursor != warmed_cursor)
    {
        warmed_cursor = S.cursor;
        for (int d = -2; d <= 2; ++d)
        {
            int k = S.cursor + d;
            if (k >= 0 && k < S.view_count)
                art_warm(&app.library.games[S.view[k]], ART_SNAP);
        }
    }
    int pending = covers_downloading();
    if (pending > 0)
    {
        char dl[64];
        snprintf(dl, sizeof(dl), tr("Getting covers (%d)"), pending);
        text_draw(TH_MARGIN, 104, 20, FONT_REGULAR, TH_TEXT_DIM, ALIGN_LEFT, dl);
    }
    const int glyphs[] = {GLYPH_CROSS, GLYPH_TRIANGLE, GLYPH_SQUARE, S.details ? GLYPH_L3 : GLYPH_R3,
                          GLYPH_TOUCHPAD};
    GameStats *hint_st = game >= 0 ? stats_get(app.library.games[game].id) : NULL;
    const char *const labels[] = {"Play", "Details", S.details ? "Choose a cover" : "Settings",
                                  S.details ? (hint_st && hint_st->hidden ? "Unhide" : "Hide") : "Favorite",
                                  "Surprise me"};
    app_draw_action_dock(glyphs, labels,
                         S.view_count ? (S.view_count > 1 && !S.details ? 5 : 4) : 1);
    /* how the pad moves around the shelf: up in the corner, because the bottom
     * of the screen is the action dock's */
    if (S.view_count > 1)
    {
        char legend[160];
        if (theme.layout == LAYOUT_GRID)
            snprintf(legend, sizeof(legend), "%s   \xc2\xb7   %s: %s", tr("L1 / R1  Category"),
                     tr("OPTIONS  Sort"), shelf_sort_name(app.global.sort_mode));
        else
            snprintf(legend, sizeof(legend), "%s   \xc2\xb7   %s   \xc2\xb7   %s   \xc2\xb7   %s: %s",
                     tr("Up / Down  Game"), tr("Left / Right  A-Z"), tr("L1 / R1  Category"),
                     tr("OPTIONS  Sort"), shelf_sort_name(app.global.sort_mode));
        app_draw_legend(legend);
    }
    plat_profile("hints");

    /* the continue dialog */
    S.dialog_t = fminf(fmaxf(S.dialog_t + (S.dialog ? app.dt : -app.dt) * 8.0f, 0.0f), 1.0f);
    if (S.dialog_t > 0.0f && game >= 0)
    {
        float t = S.dialog_t, e = 1.0f - (1.0f - t) * (1.0f - t);
        draw_rect(0, 0, plat_width(), plat_height(), argb_alpha(0xc0000000u, t));
        const float w = 1000, h = 420, x = CENTER_X - w * 0.5f, y = 330 + (1.0f - e) * 40;
        draw_rrect(x, y, w, h, TH_RADIUS, argb_alpha(TH_CARD, t));
        text_draw_fit(x + 40, y + 30, 32, FONT_BOLD, argb_alpha(TH_TEXT, t), ALIGN_LEFT, w - 80,
                      app.library.games[game].title);
        const char *labels[2] = {tr("Continue"), tr("Start over")};
        char age[64], ago[48];
        long a = S.resume_age;
        if (a < 120)
            str_copy(ago, sizeof(ago), "");
        else if (a < 3600)
            snprintf(ago, sizeof(ago), tr("%ld min ago"), a / 60);
        else if (a < 86400)
            snprintf(ago, sizeof(ago), tr("%ld h ago"), a / 3600);
        else
            snprintf(ago, sizeof(ago), tr("%ld days ago"), a / 86400);
        if (ago[0])
            snprintf(age, sizeof(age), tr("Saved %s"), ago);
        else
            str_copy(age, sizeof(age), tr("Saved just now"));
        for (int i = 0; i < 2; ++i)
        {
            float bx = x + 40 + i * (w - 80) * 0.5f, bw = (w - 80) * 0.5f - 12, by = y + 100, bh = 280;
            bool on = i == S.dialog_choice;
            draw_rrect(bx, by, bw, bh, TH_RADIUS_SMALL, argb_alpha(on ? TH_ROW_SELECTED : TH_PILL, t));
            if (on)
                draw_rrect_outline(bx, by, bw, bh, TH_RADIUS_SMALL, 3, argb_alpha(TH_FOCUS, t));
            if (i == 0 && S.resume_thumb)
                plat_draw_texture(S.resume_thumb, bx + (bw - 256) * 0.5f, by + 24, 256, 192,
                                  argb_alpha(0xffffffffu, t), false);
            else
                icon_draw(i == 0 ? ICON_PLAYER_PLAY : ICON_REFRESH, bx + bw * 0.5f - 48, by + 70, 96,
                          argb_alpha(TH_FOCUS, t));
            text_draw(bx + bw * 0.5f, by + 226, 28, FONT_BOLD, argb_alpha(TH_TEXT, t), ALIGN_CENTER,
                      labels[i]);
            if (i == 0)
                text_draw(bx + bw * 0.5f, by + bh + 14, 20, FONT_REGULAR, argb_alpha(TH_TEXT_DIM, t),
                          ALIGN_CENTER, age);
        }
    }

    picker_draw();
    if (S.launch_t > 0.0f)
        draw_rect(0, 0, plat_width(), plat_height(), argb_alpha(0xff000000u, launch));
    app_draw_toast();
}
