/*
 * GBAR5 - settings: tabs down the left, grouped rows and a caption strip, and
 * per-game settings (L2 / R2 switch between this game and all games).
 * Navigation: up / down picks a tab, Cross opens it; inside a tab every
 * direction moves between the options the way they look; Cross starts an
 * edit (left / right changes the value, Cross keeps it, Circle puts it back),
 * and Circle on the options goes back to the tabs.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "../app.h"
#include "../config.h"
#include "../core/host.h"
#include "../i18n.h"
#include "../netfs.h"
#include "../profiles.h"
#include "../platform/platform.h"
#include "../platform/xbr.h"
#include "../ra/achievements.h"
#include "../remote.h"
#include "../update.h"
#include "../../third_party/qrcodegen/qrcodegen.h"
#include "coverflow.h"
#include "draw.h"
#include "icons.h"
#include "sfx.h"
#include "text.h"
#include "theme.h"

#include "stb_image.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- the rows */

enum Kind
{
    K_CHOICE,
    K_TOGGLE,
    K_ACTION,
    K_INFO,
    K_COLOR, /* a #hex colour: edited digit by digit */
};

enum Apply
{
    APPLY_NOW,
    APPLY_NEXT_GAME,
    APPLY_NEXT_LAUNCH,
};

enum Special
{
    SP_NONE,
    SP_LANGUAGE,
    SP_HARDCORE,
    SP_UNLOCK,
    SP_ACCOUNT,
    SP_BIOS,
    SP_GAMES,
    SP_DATA,
    SP_VERSION,
    SP_REMAP,
    SP_RESCAN,
    SP_SOUND,
    SP_MEMCARDS,
    SP_REMOTE,
    SP_UPDATE,
    SP_HOTKEYS,
    SP_STATS,
    SP_THEME,
    SP_ACCENT,
    SP_BACKGROUND,
    SP_PLAYERS,
    SP_PROFILE,
    SP_WHO,
    SP_LOCATION,
    SP_NETWORK,
};

typedef struct
{
    const char *group; /* starts a new card with this caption */
    const char *name;
    const char *help;
    unsigned char kind, apply, special;
    bool global_only; /* the same for every game */
    int offset;       /* into Settings, -1 for specials */
    bool is_bool;
    const char *const *labels;
    int count, base;
} Row;

#define INT_FIELD(f) offsetof(Settings, f), false
#define BOOL_FIELD(f) offsetof(Settings, f), true
#define NO_FIELD -1, false

static const char *const OFF_ON[] = {"Off", "On"};
static const char *const ASPECTS[] = {"Auto (240 x 160)", "4:3 (a TV's shape)", "16:9", "16:10",
                                      "1:1 pixels", "Fill the screen"};
static const char *const UPSCALE[] = {"Off", "2x", "3x", "4x"};
static const char *const FILTERS[] = {"Sharp pixels", "Smooth pixels (Scale2x)", "xBR (smoothest)"};
static const char *const COVER_STYLES[] = {"Flat", "Shaded cartridge"};
static const char *const SOUND_STYLES[] = {"Soft", "Wood", "Pop", "Chime", "Classic", "Off"};
static const char *const VOLUMES[] = {"25%", "50%", "75%", "100%"};
static const char *const STICK_MODES[] = {"Auto (digital games)", "Always", "Off"};
static const char *const BACKGROUNDS[] = {"Deep emerald", "The cartridge's colours"};
static const char *const CARD_IMAGES[] = {"In-game screenshot", "Cover art"};
static const char *const PIXEL_GRIDS[] = {"Off", "Light", "Strong"};
static const char *const POPUP_STYLES[] = {"Banner", "Compact", "Big trophy"};
static const char *const RUMBLE_FEELS[] = {"Classic", "Soft", "Punchy", "Punchy, in the triggers too"};
static const char *const BRIGHTNESS[] = {"Darker", "Normal", "Brighter", "Brightest"};
static const char *const COLOURS[] = {"Natural", "Vivid", "Soft", "Warm", "Cool", "Black and white"};
static const char *const RUN_AHEADS[] = {"Off", "1 frame", "2 frames"};
static const char *const AUTOSAVES[] = {"Off", "Every 5 minutes", "Every 10 minutes", "Every 15 minutes"};
static const char *const SORTS[] = {"Title", "Recently played", "Most played", "Region"};
/* the Game Boy Advance's own screen, sound and cartridge behaviour */
static const char *const COLOUR_CORRECTION[] = {"Off (raw colours)", "Game Boy Advance", "Automatic"};
static const char *const GHOSTING[] = {"Off", "Simple", "Smart", "LCD (accurate)"};
static const char *const IDLES[] = {"Remove known", "Detect and remove", "Don't remove"};
static const char *const SOLARS[] = {"Off", "Dim", "Bright", "Full sun"};
static const char *const LOW_PASS[] = {"Off", "Soft", "Medium", "Strong"};

static const Row DISPLAY[] = {
    {"Picture", "Aspect ratio", "A Game Boy Advance picture is 240 x 160: three by two. Auto keeps that shape; filling the screen makes everything wide.", K_CHOICE,
     APPLY_NOW, SP_NONE, false, INT_FIELD(aspect), ASPECTS, 6, 0},
    {NULL, "Upscale", "Enlarges the tiny picture before it is fitted to the TV: 2x is 480 x 320, 4x is 960 x 640.", K_CHOICE, APPLY_NOW,
     SP_NONE, false, INT_FIELD(upscale), UPSCALE, 4, 1},
    {NULL, "Upscale filter", "How the pixels are treated when they are enlarged: sharp, softened, or smoothed by xBR.", K_CHOICE,
     APPLY_NOW, SP_NONE, false, INT_FIELD(upscale_filter), FILTERS, 3, 0},
    {"Screen fit", "Integer scaling", "Whole-number scale factors only: even pixels and even lines, with a border where the screen is left over.",
     K_TOGGLE, APPLY_NOW, SP_NONE, false, BOOL_FIELD(integer_scale), OFF_ON, 2, 0},
    {NULL, "Smooth final scaling", "Softens the last step up to your TV's resolution.", K_TOGGLE,
     APPLY_NOW, SP_NONE, false, BOOL_FIELD(smooth), OFF_ON, 2, 0},
    {"Game Boy Advance look", "Colour correction", "The GBA's screen was darker and richer than the raw colours. Game Boy Advance puts that back; Off shows the raw palette many emulators use.",
     K_CHOICE, APPLY_NOW, SP_NONE, false, INT_FIELD(colour_correction), COLOUR_CORRECTION, 3, 0},
    {NULL, "LCD ghosting", "The GBA's slow LCD never fully cleared between frames. F-Zero, Wave Race and the Boktai games used that for transparency; Smart only blends where it flickers.",
     K_CHOICE, APPLY_NOW, SP_NONE, false, INT_FIELD(ghosting), GHOSTING, 4, 0},
    {NULL, "Pixel grid", "Faint lines between rows of pixels, like the handheld's screen.", K_CHOICE, APPLY_NOW,
     SP_NONE, false, INT_FIELD(crt), PIXEL_GRIDS, 3, 0},
    {"Colour", "Brightness", "Makes the picture darker or brighter, for dark games or a dim TV.", K_CHOICE,
     APPLY_NOW, SP_NONE, false, INT_FIELD(brightness), BRIGHTNESS, 4, 0},
    {NULL, "Colours", "Vivid: richer colours. Soft: gentler. Warm and Cool shift the tint. Black and white, for fun.",
     K_CHOICE, APPLY_NOW, SP_NONE, false, INT_FIELD(colour), COLOURS, 6, 0},
    {"Performance", "Frameskip", "Drops a frame when the console can't keep up, so the sound stays clean.", K_TOGGLE,
     APPLY_NOW, SP_NONE, false, BOOL_FIELD(frameskip), OFF_ON, 2, 0},
    {"Overlay", "Show FPS", "Frames per second in the corner while you play.", K_TOGGLE, APPLY_NOW,
     SP_NONE, false, BOOL_FIELD(show_fps), OFF_ON, 2, 0},

};


static const Row CONTROLS[] = {
    {"Controller", "Left stick as D-pad", "Lets the left stick move the D-pad, for games played with the stick.",
     K_CHOICE, APPLY_NOW, SP_NONE, false, INT_FIELD(stick_dpad), STICK_MODES, 3, 0},
    {NULL, "Vibration", "Passes the game's rumble to your controller.", K_TOGGLE, APPLY_NOW, SP_NONE,
     false, BOOL_FIELD(rumble), OFF_ON, 2, 0},
    {NULL, "Vibration strength", "How strong the rumble feels.", K_CHOICE, APPLY_NOW, SP_NONE, false,
     INT_FIELD(rumble_strength), VOLUMES, 4, 0},
    {"DualSense", "Rumble feel", "Classic as the game sends it; Soft is gentler; Punchy makes light hits easier to feel, and can shake the triggers too.",
     K_CHOICE, APPLY_NOW, SP_NONE, false, INT_FIELD(rumble_feel), RUMBLE_FEELS, 4, 0},
    {"Buttons", "Touchpad shortcuts", "Hold the touchpad and press: R2 fast forward, L2 rewind (turn Rewind on in System), Square screenshot, Triangle start or pause the timer, Circle reset it.",
     K_INFO, APPLY_NOW, SP_HOTKEYS, true, NO_FIELD, NULL, 0, 0},
    {NULL, "Button mapping", "Choose which Game Boy Advance button each button of your controller presses.", K_ACTION,
     APPLY_NOW, SP_REMAP, false, NO_FIELD, NULL, 0, 0},
};

static const Row SOUND[] = {
    {"Game Boy Advance", "Audio filter", "Softens the GBA's harsh high frequencies, the way a small TV speaker did. Off is the raw mix.", K_CHOICE,
     APPLY_NOW, SP_NONE, false, INT_FIELD(low_pass), LOW_PASS, 4, 0},
    {"Interface", "Interface sound", "The click when you move through menus and the shelf.", K_CHOICE,
     APPLY_NOW, SP_SOUND, true, INT_FIELD(ui_sound), SOUND_STYLES, 6, 0},
    {NULL, "Interface volume", "How loud the interface sounds are.", K_CHOICE, APPLY_NOW, SP_SOUND,
     true, INT_FIELD(ui_volume), VOLUMES, 4, 0},
};

static const Row ACHIEVEMENTS[] = {
    {"Account", "Signed in as", "Sign in on the phone page (Settings > System > Settings from your phone, scan the QR, then the RetroAchievements tab), or from your PC: python tools/gbar5_ra_login.py --host <this console>. Only the token reaches the console, never the password.",
     K_INFO, APPLY_NOW, SP_ACCOUNT, true, NO_FIELD, NULL, 0, 0},
    {NULL, "Your profile", "Your points, games mastered, what you unlocked lately and the games closest to mastery.",
     K_ACTION, APPLY_NOW, SP_PROFILE, true, NO_FIELD, NULL, 0, 0},
    {"While playing", "Unlock pop-ups", "The banner when you earn an achievement. Off only hides it: achievements still unlock.",
     K_TOGGLE, APPLY_NOW, SP_NONE, true, BOOL_FIELD(ra_popups), OFF_ON, 2, 0},
    {NULL, "Pop-up style", "Banner: top right. Compact: a small line at the top. Big trophy: in the middle, like the PS5's.",
     K_CHOICE, APPLY_NOW, SP_NONE, true, INT_FIELD(ra_popup_style), POPUP_STYLES, 3, 0},
    {NULL, "Progress tracker", "The small card when a counted achievement moves (18/80 dragons).", K_TOGGLE,
     APPLY_NOW, SP_NONE, true, BOOL_FIELD(ra_tracker), OFF_ON, 2, 0},
    {"Play", "Hardcore mode", "Earn hardcore achievements: save states and cheats are off. Turning it on restarts the game.",
     K_TOGGLE, APPLY_NOW, SP_HARDCORE, true, NO_FIELD, OFF_ON, 2, 0},
};

static const Row LIBRARY[] = {
    {"Shelf", "Theme", "The look of GBAR5: ten palettes from deep Emerald to the light Pocket, or Custom with your own two colours.", K_CHOICE, APPLY_NOW, SP_THEME,
     true, INT_FIELD(theme), THEME_NAMES, THEME_COUNT, 0},
    {NULL, "Accent colour", "Any #hex colour you like: the focus ring, the switches and the highlighted row. Cross starts editing; Up / Down changes the digit, Left / Right picks the next one.", K_COLOR, APPLY_NOW, SP_ACCENT,
     true, NO_FIELD, NULL, 0, 0},
    {NULL, "Background colour", "The ground the whole app sits on. Keep it dark and the text turns light; pick a light one and the text turns dark by itself.", K_COLOR, APPLY_NOW, SP_BACKGROUND,
     true, NO_FIELD, NULL, 0, 0},
    {NULL, "Cover art", "Flat label art, or a shaded cartridge around it.", K_CHOICE, APPLY_NOW, SP_NONE, true,
     INT_FIELD(cover_style), COVER_STYLES, 2, 0},
    {NULL, "Card image", "What the box on the left shows: the game's in-game screenshot, so you can see what it looks like, or the cartridge's cover art. The background behind the shelf doesn't change.",
     K_CHOICE, APPLY_NOW, SP_NONE, true, INT_FIELD(shelf_card), CARD_IMAGES, 2, 0},
    {NULL, "Download missing covers", "Fetches covers for new games over the internet.", K_TOGGLE,
     APPLY_NOW, SP_NONE, true, BOOL_FIELD(cover_download), OFF_ON, 2, 0},
    {NULL, "Background", "Deep emerald keeps the screen calm; the cartridge's colours tints it with the game you have selected.",
     K_CHOICE, APPLY_NOW, SP_NONE, true, INT_FIELD(background), BACKGROUNDS, 2, 0},
    {NULL, "Sort by", "The order of games on the shelf (OPTIONS changes it there too).", K_CHOICE,
     APPLY_NOW, SP_NONE, true, INT_FIELD(sort_mode), SORTS, 4, 0},
    {"Games", "Your library", "How much you've played: games, hours, achievements and your most played.", K_ACTION,
     APPLY_NOW, SP_STATS, true, NO_FIELD, NULL, 0, 0},
    {NULL, "Rescan library", "Looks for games added to /data/GBAR5/games or a USB drive.", K_ACTION,
     APPLY_NOW, SP_RESCAN, true, NO_FIELD, NULL, 0, 0},

};

static const Row SYSTEM[] = {
    {"Playing", "Quick resume", "Saves when you leave a game, so the shelf can offer Continue.", K_TOGGLE,
     APPLY_NOW, SP_NONE, true, BOOL_FIELD(quick_resume), OFF_ON, 2, 0},
    {NULL, "Rewind", "Keeps the last 8 seconds so you can go back (touchpad + L2). Uses about 200 MB of memory.",
     K_TOGGLE, APPLY_NOW, SP_NONE, false, BOOL_FIELD(rewind), OFF_ON, 2, 0},
    {NULL, "Run-ahead", "Answers your buttons 1 or 2 frames sooner, for fighting games and platformers. It runs extra frames: at high internal resolutions some games may slow down.",
     K_CHOICE, APPLY_NOW, SP_NONE, false, INT_FIELD(run_ahead), RUN_AHEADS, 3, 0},
    {NULL, "Auto-save", "Saves the game by itself every few minutes into three auto-save slots (the oldest is replaced). Load one from the in-game menu > Auto-saves.",
     K_CHOICE, APPLY_NOW, SP_NONE, false, INT_FIELD(autosave), AUTOSAVES, 4, 0},
    {"Cartridge", "Use a real BIOS", "Boots through gba_bios.bin in the bios folder. Only a few games need it; Off uses the built-in start.",
     K_TOGGLE, APPLY_NEXT_LAUNCH, SP_NONE, false, BOOL_FIELD(use_bios), OFF_ON, 2, 0},
    {NULL, "Skip the boot logo", "Off: the Game Boy Advance startup animation before the game. On: straight in.",
     K_TOGGLE, APPLY_NEXT_LAUNCH, SP_NONE, false, BOOL_FIELD(skip_bios), OFF_ON, 2, 0},
    {NULL, "Idle loop removal", "Skips the CPU's empty wait loops: free speed, and nothing visible changes.", K_CHOICE,
     APPLY_NEXT_LAUNCH, SP_NONE, false, INT_FIELD(idle_opt), IDLES, 3, 0},
    {NULL, "Solar sensor", "For cartridges with a light sensor (Boktai): how much sun the game sees.", K_CHOICE,
     APPLY_NOW, SP_NONE, false, INT_FIELD(solar), SOLARS, 4, 0},
    {NULL, "Game Boy Player rumble", "Lets games with the Game Boy Player logo use your controller's rumble. A few flicker with it on.",
     K_TOGGLE, APPLY_NEXT_LAUNCH, SP_NONE, false, BOOL_FIELD(gbp_rumble), OFF_ON, 2, 0},
    {NULL, "Opposing directions", "Allows left and right at once. Off, like the real D-pad; some games glitch with it on.",
     K_TOGGLE, APPLY_NOW, SP_NONE, false, BOOL_FIELD(opposing), OFF_ON, 2, 0},
    {"People", "Who's playing", "Profiles: each person keeps their own saves, save states, settings, play time, favourites and RetroAchievements sign-in. Games and covers are shared.",
     K_ACTION, APPLY_NOW, SP_WHO, true, NO_FIELD, NULL, 0, 0},
    {"Phone", "Settings from your phone", "Change GBAR5's settings from a phone on the same network: scan the QR on the card below, or type the code that sits beside it.",
     K_TOGGLE, APPLY_NOW, SP_REMOTE, true, BOOL_FIELD(remote), OFF_ON, 2, 0},
    {"Console", "Language", "The language of GBAR5's menus.", K_CHOICE, APPLY_NOW, SP_LANGUAGE, true,
     INT_FIELD(language), NULL, LANG_COUNT, 0},
    {NULL, "Unlock /data with etaHEN", "GBAR5 asks etaHEN for access to your games. Turn off if closing GBAR5 crashes the console.",
     K_TOGGLE, APPLY_NEXT_LAUNCH, SP_UNLOCK, true, NO_FIELD, OFF_ON, 2, 0},
    {NULL, "Network shares", "Games on a PC or NAS, played straight from it. List each shared folder in network.cfg in GBAR5's data folder, one a line: smb://192.168.1.10/Games/GBA user=name password=secret (Windows, NAS), or nfs://192.168.1.10/volume1/gba. Press Cross to look through them again.",
     K_ACTION, APPLY_NOW, SP_NETWORK, true, NO_FIELD, NULL, 0, 0},
    {NULL, "Data location", "Where GBAR5 keeps its saves, states, settings, covers and downloads: the console's storage or an extended / USB drive (in its GBAR5 folder). Press Cross for the next one; your files are copied there, and it's used from the next start. Games stay where they are and are still found.",
     K_ACTION, APPLY_NEXT_LAUNCH, SP_LOCATION, true, NO_FIELD, NULL, 0, 0},

};

static const Row ABOUT[] = {
    {"GBAR5", "Version", "GBAR5: Game Boy Advance emulation for the PS5. Every piece of art in the app is drawn from scratch.", K_INFO, APPLY_NOW, SP_VERSION, true, NO_FIELD, NULL, 0, 0},
    {NULL, "Games", "Games are your own backups, in /data/GBAR5/games.", K_INFO, APPLY_NOW, SP_GAMES,
     true, NO_FIELD, NULL, 0, 0},
    {NULL, "Data folder", "Saves, states, covers and settings live here.", K_INFO, APPLY_NOW, SP_DATA,
     true, NO_FIELD, NULL, 0, 0},
    {"Updates", "Check for updates", "Looks for a newer GBAR5 and installs it.", K_ACTION, APPLY_NOW,
     SP_UPDATE, true, NO_FIELD, NULL, 0, 0},
    {NULL, "Check when GBAR5 starts", "Tells you on the shelf when a new version is out.", K_TOGGLE, APPLY_NOW,
     SP_NONE, true, BOOL_FIELD(update_check), OFF_ON, 2, 0},

};

typedef struct
{
    const char *name;
    int icon;
    const Row *rows;
    int count;
} Tab;

#define TAB(n, i, r) {n, i, r, (int)(sizeof(r) / sizeof(r[0]))}
static const Tab TABS[] = {
    TAB("Screen", ICON_DEVICE_TV, DISPLAY),
    TAB("Controls", ICON_DEVICE_GAMEPAD_2, CONTROLS),
    TAB("Sound", ICON_VOLUME, SOUND),
    TAB("Shelf", ICON_BOOKS, LIBRARY),
    TAB("System", ICON_CPU, SYSTEM),
    TAB("About", ICON_INFO_CIRCLE, ABOUT),
};
#define TAB_COUNT (int)(sizeof(TABS) / sizeof(TABS[0]))
#define DISPLAY_COUNT (int)(sizeof(DISPLAY) / sizeof(DISPLAY[0]))
#define CONTROLS_COUNT (int)(sizeof(CONTROLS) / sizeof(CONTROLS[0]))
#define SOUND_COUNT (int)(sizeof(SOUND) / sizeof(SOUND[0]))
#define LIBRARY_COUNT (int)(sizeof(LIBRARY) / sizeof(LIBRARY[0]))
#define SYSTEM_COUNT (int)(sizeof(SYSTEM) / sizeof(SYSTEM[0]))
#define ABOUT_COUNT (int)(sizeof(ABOUT) / sizeof(ABOUT[0]))

/* ---------------------------------------------------------------- state */

static struct
{
    int tab, cursor;
    bool rail;       /* the tab rail has the focus */
    bool editing;    /* the chosen setting is being changed */
    int edit_orig;   /* its value when the edit began */
    int hex_pos;     /* a #hex colour: which of the six digits is chosen */
    uint32_t hex_orig; /* the colour when that edit began */
    int hex_theme;   /* the theme before a colour edit switched to Custom */
    bool hex_was_dirty; /* whether there were unsaved changes before it */
    float row_x[32], row_y[32], row_w[32], row_h[32]; /* where each row was drawn */
    int row_rects;
    bool game_scope; /* editing this game's own settings */
    bool remap;      /* the button-mapping page */
    bool remap_edit; /* a button mapping is being changed */
    int remap_orig;  /* its mapping when the edit began */
    bool players;    /* the player-order page */
    bool who;        /* the profiles page */
    int who_cursor;
    float who_y;
    int order[PSXS5_MAX_PADS], assigned; /* controllers chosen so far, in player order */
    uint32_t pad_prev[PSXS5_MAX_PADS];
    int remap_cursor;
    float tab_x, tab_w, sel_y, scroll, scope_x, remap_y, rail_y;
    float switch_t[16];
    /* Save / Cancel: nothing is written until Save, and Cancel puts every
     * setting back the way it was when Settings opened. */
    int action;      /* -1 nothing, 0 = Save has the focus, 1 = Cancel */
    bool dirty;      /* something was changed since the last save */
    bool drop_game;  /* "use the settings for all games" waits for Save */
    bool confirm;    /* leaving with unsaved changes: the Save? pop-up is up */
    int confirm_choice; /* 0 = Yes (save), 1 = No (leave without saving) */
    float confirm_t;    /* the pop-up's entrance, 0..1 */
    Settings orig_global, orig_settings;
    bool orig_has_own, orig_game_scope, orig_unlock;
} S;

void settings_opened(void)
{
    S.cursor = -1;
    S.rail = true;
    S.editing = false;
    S.row_rects = 0;
    S.remap = false;
    S.remap_edit = false;
    S.players = false;
    S.who = false;
    S.game_scope = app.game != NULL && app.game_has_own;
    S.tab_x = 0;
    S.sel_y = -1;
    S.scroll = 0;
    S.rail_y = -1;
    S.action = -1;
    S.dirty = false;
    S.drop_game = false;
    S.confirm = false;
    S.confirm_choice = 0;
    S.confirm_t = 0;
    S.orig_global = app.global;
    S.orig_settings = app.settings;
    S.orig_has_own = app.game_has_own;
    S.orig_game_scope = S.game_scope;
    S.orig_unlock = app.unlock_setting;
}

static const Tab *tab(void)
{
    return &TABS[S.tab];
}

static bool selectable(const Row *r)
{
    return r->kind != K_INFO;
}

static void first_selectable(void)
{
    S.cursor = -1;
    for (int i = 0; i < tab()->count; ++i)
        if (selectable(&tab()->rows[i]))
        {
            S.cursor = i;
            break;
        }
}

/* Each tab records where it drew its rows; the D-pad then moves to the
 * nearest row in that direction, so grids, columns and faders all move the
 * way they look on screen. */
static void note_row(int row, float x, float y, float w, float h)
{
    if (row < 0 || row >= (int)(sizeof(S.row_x) / sizeof(S.row_x[0])))
        return;
    S.row_x[row] = x;
    S.row_y[row] = y;
    S.row_w[row] = w;
    S.row_h[row] = h;
    if (row + 1 > S.row_rects)
        S.row_rects = row + 1;
}

static bool row_focused(int row)
{
    return S.action < 0 && !S.rail && row == S.cursor;
}

/* The nearest focusable row in the pressed direction; false when there is
 * nothing that way (so Up can reach the Save / Cancel buttons). */
static bool move_focus(int dx, int dy)
{
    const Tab *t = tab();
    if (S.cursor < 0 || S.cursor >= t->count)
    {
        first_selectable();
        return true;
    }
    if (S.cursor >= S.row_rects)
    {
        /* the layout isn't recorded yet: fall back to the list order */
        if (!dy)
            return false;
        int c = S.cursor;
        do
            c = (c + dy + t->count) % t->count;
        while (!selectable(&t->rows[c]) && c != S.cursor);
        if (c != S.cursor)
        {
            S.cursor = c;
            sfx_play(SFX_CLICK);
            return true;
        }
        return false;
    }
    float cx = S.row_x[S.cursor] + S.row_w[S.cursor] * 0.5f;
    float cy = S.row_y[S.cursor] + S.row_h[S.cursor] * 0.5f;
    int best = -1;
    float best_score = 0;
    /* Two passes. The first only considers rows that share this one's band, so
     * a side-by-side pair beats a diagonal neighbour that is merely nearer: the
     * two #hex fields sit level with each other, and without this Left from
     * Background picked the Card image tile below it instead of the Accent
     * field right beside it. If nothing shares the band (an offset layout),
     * the second pass falls back to the plain nearest-row rule. */
    for (int pass = 0; pass < 2 && best < 0; ++pass)
    {
        for (int i = 0; i < t->count && i < S.row_rects; ++i)
        {
            if (i == S.cursor || !selectable(&t->rows[i]) || S.row_w[i] <= 0)
                continue;
            float ax = S.row_x[i] + S.row_w[i] * 0.5f - cx;
            float ay = S.row_y[i] + S.row_h[i] * 0.5f - cy;
            float along = dx ? ax * dx : ay * dy;
            if (along < 4.0f)
                continue; /* not in that direction */
            if (pass == 0)
            {
                float lo = dx ? fmaxf(S.row_y[i], S.row_y[S.cursor])
                              : fmaxf(S.row_x[i], S.row_x[S.cursor]);
                float hi = dx ? fminf(S.row_y[i] + S.row_h[i],
                                      S.row_y[S.cursor] + S.row_h[S.cursor])
                              : fminf(S.row_x[i] + S.row_w[i],
                                      S.row_x[S.cursor] + S.row_w[S.cursor]);
                if (hi - lo < 8.0f)
                    continue; /* sits off the band: only a fallback */
            }
            float across = dx ? fabsf(ay) : fabsf(ax);
            float score = along + across * 2.5f;
            if (best < 0 || score < best_score)
            {
                best = i;
                best_score = score;
            }
        }
    }
    if (best >= 0)
    {
        S.cursor = best;
        sfx_play(SFX_CLICK);
        return true;
    }
    return false;
}

/* ---------------------------------------------------------------- save / cancel */

static void apply_settings_now(void)
{
    theme_customize(app.global.custom_bg, app.global.custom_accent);
    theme_apply(app.global.theme);
    i18n_set(app.global.language);
    remote_update(app.global.remote);
    sfx_configure(app.global.ui_sound, (app.global.ui_volume + 1) * 25);
    plat_set_unlock_disabled(!app.unlock_setting);
    if (app.game)
        host_apply_settings(&app.settings);
}

/* Write everything that was changed, and make that the new starting point. */
static void settings_commit(void)
{
    if (S.drop_game && app.game)
    {
        char path[PSXS5_PATH_MAX];
        app_game_config_path(path, sizeof(path), app.game);
        remove(path);
        S.drop_game = false;
    }
    app_save_settings();
    S.orig_global = app.global;
    S.orig_settings = app.settings;
    S.orig_has_own = app.game_has_own;
    S.orig_game_scope = S.game_scope;
    S.orig_unlock = app.unlock_setting;
    S.dirty = false;
    sfx_play(SFX_SELECT);
    app_toast(tr("Settings saved"));
}

/* Put every setting back the way it was when Settings opened. */
static void settings_discard(void)
{
    app.global = S.orig_global;
    app.settings = S.orig_settings;
    app.unlock_setting = S.orig_unlock;
    app.game_has_own = S.orig_has_own;
    S.game_scope = S.orig_game_scope;
    S.drop_game = false;
    S.dirty = false;
    apply_settings_now();
}

/* Leave Settings: with unsaved changes, ask first; otherwise go straight back. */
static void settings_leave(void)
{
    if (S.dirty)
    {
        S.confirm = true;
        S.confirm_choice = 0;
        sfx_play(SFX_SELECT);
        return;
    }
    app.screen = app.settings_return;
}

/* The answer to "Save settings?": Yes writes them, No drops them, then leave. */
static void confirm_close(bool save)
{
    S.confirm = false;
    if (save)
        settings_commit();
    else
    {
        settings_discard();
        app_toast(tr("Changes discarded"));
    }
    app.screen = app.settings_return;
}

/* The settings a row edits: this game's own, or the console-wide ones. */
static Settings *target(const Row *r)
{
    return (!r->global_only && S.game_scope && app.game) ? &app.settings : &app.global;
}

/* ---------------------------------------------------------------- the Custom theme's two colours */

static uint32_t colour_of(const Row *r)
{
    return r->special == SP_ACCENT ? theme_custom_accent() : theme_custom_bg();
}

static void apply_custom_theme(void)
{
    theme_customize(app.global.custom_bg, app.global.custom_accent);
    theme_apply(app.global.theme);
}

/* Set one of the two Custom colours and switch to the Custom theme, so the
 * change is visible on this frame. */
static void set_colour(const Row *r, uint32_t colour)
{
    if (r->special == SP_ACCENT)
        app.global.custom_accent = colour & 0xffffffu;
    else
        app.global.custom_bg = colour & 0xffffffu;
    app.global.theme = THEME_CUSTOM;
    apply_custom_theme();
    S.dirty = true;
}

/* The digit the D-pad is on: 0 is the first hex digit (the most significant).
 * Up / Down turns it; the other five stay. */
static void hex_step(const Row *r, int dir)
{
    uint32_t c = colour_of(r);
    int shift = 20 - S.hex_pos * 4;
    int d = (int)((c >> shift) & 0xfu);
    d = (d + (dir > 0 ? 1 : 15)) & 0xf;
    c = (c & ~(0xfu << shift)) | ((uint32_t)d << shift);
    set_colour(r, c);
}

static int read_value(const Row *r)
{
    switch (r->special)
    {
    case SP_HARDCORE: return ra_hardcore();
    case SP_UNLOCK: return app.unlock_setting;
    default: break;
    }
    if (r->offset < 0)
        return 0;
    const char *base = (const char *)target(r) + r->offset;
    return r->is_bool ? (*(const bool *)base ? 1 : 0) : *(const int *)base - r->base;
}

static void write_value(const Row *r, int v)
{
    switch (r->special)
    {
    case SP_HARDCORE:
        if (!ra_user()[0])
        {
            app_toast("Sign in first: Settings > Achievements > Account");
            return;
        }
        ra_set_hardcore(v != 0); /* turning it on restarts the game */
        if (v)
            cheats_clear(&app.cheats);
        return;
    case SP_UNLOCK:
        app.unlock_setting = v != 0;
        plat_set_unlock_disabled(!app.unlock_setting);
        return;
    default:
        break;
    }
    if (r->offset < 0)
        return;
    Settings *t = target(r);
    char *base = (char *)t + r->offset;
    if (r->is_bool)
        *(bool *)base = v != 0;
    else
        *(int *)base = v + r->base;
    /* console-wide edits reach the running game unless it has its own settings */
    if (t == &app.global && (!app.game || !app.game_has_own || r->global_only))
    {
        char *eff = (char *)&app.settings + r->offset;
        if (r->is_bool)
            *(bool *)eff = v != 0;
        else
            *(int *)eff = v + r->base;
    }
    if (r->special == SP_LANGUAGE)
        i18n_set(app.global.language);
    if (r->special == SP_THEME)
    {
        theme_customize(app.global.custom_bg, app.global.custom_accent);
        theme_apply(app.global.theme);
    }
    if (r->special == SP_REMOTE)
        remote_update(app.global.remote);
    if (r->special == SP_SOUND)
        sfx_configure(app.global.ui_sound, (app.global.ui_volume + 1) * 25);
    if (app.game)
        host_apply_settings(&app.settings);
    S.dirty = true;
}

/* A choice or a toggle, one step along its list. */
static void step_value(const Row *r, int delta)
{
    if (r->kind != K_CHOICE && r->kind != K_TOGGLE)
        return;
    int count = r->kind == K_TOGGLE ? 2 : r->count;
    if (count < 1)
        return;
    write_value(r, (read_value(r) + count + delta) % count);
}

static void begin_edit(const Row *r)
{
    S.editing = true;
    S.edit_orig = read_value(r);
    if (r->kind == K_COLOR)
    {
        S.hex_pos = 0;
        S.hex_orig = colour_of(r);
        S.hex_theme = app.global.theme;
        S.hex_was_dirty = S.dirty;
    }
    sfx_play(SFX_SELECT);
}

static void end_edit(const Row *r, bool keep)
{
    if (r && r->kind == K_COLOR)
    {
        if (!keep)
        {
            /* put the colour, the theme and the "unsaved" flag back as they were */
            if (r->special == SP_ACCENT)
                app.global.custom_accent = S.hex_orig;
            else
                app.global.custom_bg = S.hex_orig;
            app.global.theme = S.hex_theme;
            apply_custom_theme();
            S.dirty = S.hex_was_dirty;
        }
        else if (colour_of(r) == S.hex_orig && app.global.theme == S.hex_theme)
            S.dirty = S.hex_was_dirty;
        S.editing = false;
        sfx_play(keep ? SFX_SELECT : SFX_BACK);
        return;
    }
    if (!keep && r && read_value(r) != S.edit_orig)
        write_value(r, S.edit_orig);
    S.editing = false;
    sfx_play(keep ? SFX_SELECT : SFX_BACK);
}

static const char *value_label(const Row *r, char *buf, size_t size)
{
    int v = read_value(r);
    switch (r->special)
    {
    case SP_LANGUAGE: return i18n_name(v);
    case SP_ACCOUNT:
        if (!ra_user()[0])
            return tr("Not signed in");
        snprintf(buf, size, "%s%s", ra_user(), ra_signed_in() ? "" : tr(" (not signed in)"));
        return buf;
    case SP_VERSION: return PSXS5_VERSION;
    case SP_BIOS: app_describe_bios(buf, size); return buf;
    case SP_GAMES: snprintf(buf, size, "%d", app.library.count); return buf;
    case SP_DATA: return app.paths.root;
    case SP_NETWORK:
    {
        int count = netfs_share_count(), up = 0;
        for (int i = 0; i < count; ++i)
            up += !strcmp(netfs_share_status(i), "connected");
        if (!count)
            return tr("None");
        snprintf(buf, size, tr("%d of %d connected"), up, count);
        return buf;
    }
    case SP_LOCATION:
        if (!app.location_next[0])
            return app.paths.root;
        snprintf(buf, size, tr("%s (from the next start)"), app.location_next);
        return buf;
    case SP_HOTKEYS: return tr("Touchpad + R2 / L2");
    case SP_UPDATE:
        switch (update_state())
        {
        case UPDATE_CHECKING: return tr("Checking...");
        case UPDATE_NONE: return tr("Up to date");
        case UPDATE_AVAILABLE: snprintf(buf, size, tr("%s available: install"), update_version()); return buf;
        case UPDATE_INSTALLING: return tr("Installing...");
        case UPDATE_INSTALLED: return tr("Installed: restart Game Boy Advance");
        case UPDATE_FAILED: return update_message();
        default: return PSXS5_VERSION;
        }
    default: break;
    }
    if (!r->labels || v < 0 || v >= r->count)
        return "";
    return tr(r->labels[v]);
}

static void default_value(const Row *r)
{
    if (r->offset < 0 && r->kind != K_COLOR)
        return;
    Settings d;
    config_defaults(&d);
    if (r->kind == K_COLOR)
    {
        set_colour(r, r->special == SP_ACCENT ? d.custom_accent : d.custom_bg);
        return;
    }
    const char *base = (const char *)&d + r->offset;
    write_value(r, r->is_bool ? (*(const bool *)base ? 1 : 0) : *(const int *)base - r->base);
}

/* ---------------------------------------------------------------- the button-mapping page */

typedef struct
{
    int button; /* BTN_* on the controller */
    int glyph;
    const char *name;
    float x, y; /* on the controller drawing, relative to its centre */
} PadButton;

static const PadButton PAD[] = {
    {BTN_CROSS, GLYPH_CROSS, "Cross", 230, 46},
    {BTN_CIRCLE, GLYPH_CIRCLE, "Circle", 290, -14},
    {BTN_SQUARE, GLYPH_SQUARE, "Square", 170, -14},
    {BTN_TRIANGLE, GLYPH_TRIANGLE, "Triangle", 230, -74},
    {BTN_UP, GLYPH_UP, "Up", -230, -70},
    {BTN_DOWN, GLYPH_DOWN, "Down", -230, 42},
    {BTN_LEFT, GLYPH_LEFT, "Left", -286, -14},
    {BTN_RIGHT, GLYPH_RIGHT, "Right", -174, -14},
    {BTN_L1, GLYPH_L1, "L1", -230, -168},
    {BTN_R1, GLYPH_R1, "R1", 230, -168},
    {BTN_L2, GLYPH_L2, "L2", -230, -226},
    {BTN_R2, GLYPH_R2, "R2", 230, -226},
    {BTN_L3, GLYPH_L3, "L3 (left stick)", -110, 110},
    {BTN_R3, GLYPH_R3, "R3 (right stick)", 110, 110},
    {BTN_START, GLYPH_START, "OPTIONS", 214, -122},
};
#define PAD_COUNT (int)(sizeof(PAD) / sizeof(PAD[0]))

/* What a mapping can point at: the Game Boy Advance's buttons (RetroPad A is
 * the GBA's A, which lives on Circle; RetroPad B is the GBA's B, on Cross),
 * and the pad's own extra buttons for anyone who wants them. */
static const struct
{
    int button, glyph;
    const char *name;
} PS1[] = {
    {BTN_CIRCLE, GLYPH_CIRCLE, "A"},        {BTN_CROSS, GLYPH_CROSS, "B"},
    {BTN_L1, GLYPH_L1, "L"},                {BTN_R1, GLYPH_R1, "R"},
    {BTN_START, GLYPH_START, "Start"},      {BTN_SELECT, GLYPH_SELECT, "Select"},
    {BTN_UP, GLYPH_UP, "Up"},               {BTN_DOWN, GLYPH_DOWN, "Down"},
    {BTN_LEFT, GLYPH_LEFT, "Left"},         {BTN_RIGHT, GLYPH_RIGHT, "Right"},
    {BTN_SQUARE, GLYPH_SQUARE, "Y"},        {BTN_TRIANGLE, GLYPH_TRIANGLE, "X"},
    {BTN_L2, GLYPH_L2, "L2"},               {BTN_R2, GLYPH_R2, "R2"},
    {BTN_L3, GLYPH_L3, "L3"},               {BTN_R3, GLYPH_R3, "R3"},
    {-1, GLYPH_NONE, "Nothing"},
};
#define PS1_COUNT (int)(sizeof(PS1) / sizeof(PS1[0]))

static int ps1_index(int button)
{
    for (int i = 0; i < PS1_COUNT; ++i)
        if (PS1[i].button == button)
            return i;
    return PS1_COUNT - 1;
}

static Settings *map_target(void)
{
    return (S.game_scope && app.game) ? &app.settings : &app.global;
}

static void set_map(int button, int to)
{
    map_target()->button_map[button] = (int8_t)to;
    if (map_target() == &app.global && (!app.game || !app.game_has_own))
        app.settings.button_map[button] = (int8_t)to;
    S.dirty = true;
}

static const char *const PRESETS[] = {"Default", "Swap Cross and Circle", "Swap shoulders and triggers"};

static void apply_preset(int preset)
{
    for (int b = 0; b < 16; ++b)
        set_map(b, b);
    if (preset == 1)
    {
        set_map(BTN_CROSS, BTN_CIRCLE);
        set_map(BTN_CIRCLE, BTN_CROSS);
    }
    else if (preset == 2)
    {
        set_map(BTN_L1, BTN_L2);
        set_map(BTN_L2, BTN_L1);
        set_map(BTN_R1, BTN_R2);
        set_map(BTN_R2, BTN_R1);
    }
    char msg[96];
    snprintf(msg, sizeof(msg), tr("Mapping: %s"), tr(PRESETS[preset]));
    app_toast(msg);
}

static void draw_controller(float cx, float cy, int highlight)
{
    /* a DualSense, simplified: body, grips, touchpad, sticks */
    const uint32_t body = TH_PILL, edge = argb_lerp(TH_BG_DEEP, TH_FOCUS, 0.42f);
    draw_rrect(cx - 360, cy - 130, 720, 250, 120, edge);
    draw_rrect(cx - 354, cy - 124, 708, 238, 116, body);
    draw_circle(cx - 250, cy + 140, 92, edge);
    draw_circle(cx + 250, cy + 140, 92, edge);
    draw_circle(cx - 250, cy + 140, 86, body);
    draw_circle(cx + 250, cy + 140, 86, body);
    draw_rrect(cx - 354, cy - 60, 708, 170, 60, body);
    draw_rrect(cx - 110, cy - 118, 220, 110, 18, TH_BG_DEEP); /* touchpad */
    text_draw(cx, cy + 276, 18, FONT_REGULAR, TH_TEXT_DIM, ALIGN_CENTER, tr("Touchpad: tap = Select, hold = menu"));
    for (int s = -1; s <= 1; s += 2)
    {
        draw_circle(cx + s * 110, cy + 110, 46, TH_BG);
        draw_circle(cx + s * 110, cy + 110, 34, edge);
    }
    for (int i = 0; i < PAD_COUNT; ++i)
    {
        float x = cx + PAD[i].x, y = cy + PAD[i].y;
        bool on = i == highlight;
        if (on)
        {
            float pulse = 0.6f + 0.4f * sinf((float)plat_ticks_us() / 1e6f * 6.0f);
            float w = pad_glyph_width((enum PadGlyph)PAD[i].glyph, 44) + 20;
            draw_rrect(x - w * 0.5f, y - 32, w, 64, 32, argb_alpha(TH_FOCUS, 0.35f * pulse));
            draw_rrect_outline(x - w * 0.5f, y - 32, w, 64, 32, 3, TH_FOCUS);
        }
        draw_pad_glyph((enum PadGlyph)PAD[i].glyph, x, y, 44);
    }
}

static void remap_page(uint32_t pressed)
{
    const PadButton *pb = &PAD[S.remap_cursor];
    int current = ps1_index(map_target()->button_map[pb->button]);

    if (S.remap_edit)
    {
        /* left / right picks what this button sends; Cross keeps it */
        int step = (pressed & BIT(BTN_RIGHT)) ? 1 : (pressed & BIT(BTN_LEFT)) ? -1 : 0;
        if (step)
        {
            current = (current + step + PS1_COUNT) % PS1_COUNT;
            set_map(pb->button, PS1[current].button);
            sfx_play(SFX_CLICK);
        }
        if (pressed & BIT(BTN_CROSS))
        {
            S.remap_edit = false;
            sfx_play(SFX_SELECT);
        }
        else if (pressed & BIT(BTN_CIRCLE))
        {
            set_map(pb->button, S.remap_orig);
            S.remap_edit = false;
            sfx_play(SFX_BACK);
        }
    }
    else
    {
        int before = S.remap_cursor;
        if (pressed & BIT(BTN_UP))
            S.remap_cursor = (S.remap_cursor + PAD_COUNT - 1) % PAD_COUNT;
        if (pressed & BIT(BTN_DOWN))
            S.remap_cursor = (S.remap_cursor + 1) % PAD_COUNT;
        if (S.remap_cursor != before)
            sfx_play(SFX_CLICK);
        pb = &PAD[S.remap_cursor];
        if (pressed & BIT(BTN_CROSS))
        {
            S.remap_edit = true;
            S.remap_orig = map_target()->button_map[pb->button];
            sfx_play(SFX_SELECT);
        }
        if (pressed & BIT(BTN_TRIANGLE))
        {
            set_map(pb->button, pb->button);
            sfx_play(SFX_CLICK);
        }
        static int preset;
        if (pressed & BIT(BTN_SQUARE))
        {
            preset = (preset + 1) % 3;
            apply_preset(preset);
            sfx_play(SFX_SELECT);
        }
        if (pressed & BIT(BTN_CIRCLE))
        {
            S.remap = false;
            sfx_play(SFX_BACK);
            return;
        }
    }

    draw_controller(800, 560, S.remap_cursor);

    /* the list */
    const float x = 1260, w = 600, top = 170, row_h = 50;
    draw_rrect(x - 16, top - 16, w + 32, PAD_COUNT * row_h + 32, TH_RADIUS, TH_CARD);
    float target_y = top + S.remap_cursor * row_h;
    if (S.remap_y == 0)
        S.remap_y = target_y;
    anim_approach(&S.remap_y, target_y, app.dt, TH_SNAP);
    draw_rrect(x - 4, S.remap_y, w + 8, row_h - 4, 10, TH_ROW_SELECTED);
    if (S.remap_edit)
    {
        float pulse = 0.55f + 0.45f * sinf((float)plat_ticks_us() / 1e6f * 5.0f);
        draw_rrect_outline(x - 8, S.remap_y - 4, w + 16, row_h + 4, 12, 2.5f,
                           argb_alpha(TH_FOCUS, 0.45f + 0.45f * pulse));
    }
    for (int i = 0; i < PAD_COUNT; ++i)
    {
        float y = top + i * row_h;
        const PadButton *b = &PAD[i];
        int to = ps1_index(map_target()->button_map[b->button]);
        bool changed = map_target()->button_map[b->button] != b->button;
        draw_pad_glyph((enum PadGlyph)b->glyph, x + 52, y + row_h * 0.45f, 28);
        text_draw(x + 108, y + 9, 22, FONT_REGULAR, TH_TEXT, ALIGN_LEFT, tr(b->name));
        icon_draw(ICON_ARROWS_EXCHANGE, x + 318, y + 9, 26, changed ? TH_FOCUS : TH_TEXT_DIM);
        draw_pad_glyph((enum PadGlyph)PS1[to].glyph, x + 408, y + row_h * 0.45f, 28);
        text_draw(x + 464, y + 9, 22, FONT_REGULAR, changed ? TH_FOCUS : TH_TEXT_SOFT, ALIGN_LEFT,
                  tr(PS1[to].name));
    }

    if (S.remap_edit)
    {
        static const int glyphs[] = {GLYPH_LEFT, GLYPH_CROSS, GLYPH_CIRCLE};
        static const char *const labels[] = {"Change", "Save", "Cancel"};
        app_draw_hints(glyphs, labels, 3, "Left / Right changes the button");
    }
    else
    {
        static const int glyphs[] = {GLYPH_CROSS, GLYPH_TRIANGLE, GLYPH_SQUARE, GLYPH_CIRCLE};
        static const char *const labels[] = {"Select", "Reset button", "Presets", "Back"};
        app_draw_hints(glyphs, labels, 4, "Up / Down  Buttons");
    }
}

/* ---------------------------------------------------------------- the player-order page */

/* Each controller presses Cross in turn: the first is player 1, and so on. */
static void players_page(uint32_t pressed)
{
    int current[PSXS5_MAX_PADS], connected = 0;
    plat_player_order(current);
    for (int k = 0; k < PSXS5_MAX_PADS; ++k)
        connected += app.pads[k].connected;
    for (int k = 0; k < PSXS5_MAX_PADS; ++k)
    {
        uint32_t now = app.pads[k].buttons, newly = now & ~S.pad_prev[k];
        S.pad_prev[k] = now;
        if (!(newly & BIT(BTN_CROSS)) || !app.pads[k].connected)
            continue;
        int controller = current[k];
        bool taken = false;
        for (int i = 0; i < S.assigned; ++i)
            taken |= S.order[i] == controller;
        if (!taken && S.assigned < PSXS5_MAX_PADS)
        {
            S.order[S.assigned++] = controller;
            sfx_play(SFX_SELECT);
        }
    }
    if (S.assigned > 0 && S.assigned >= connected)
    {
        /* the controllers that aren't here take the places left */
        int order[PSXS5_MAX_PADS], n = S.assigned;
        memcpy(order, S.order, sizeof(order));
        for (int c = 0; c < PSXS5_MAX_PADS && n < PSXS5_MAX_PADS; ++c)
        {
            bool used = false;
            for (int i = 0; i < n; ++i)
                used |= order[i] == c;
            if (!used)
                order[n++] = c;
        }
        plat_set_player_order(order);
        S.players = false;
        app_toast("Player order set");
        return;
    }
    if (pressed & BIT(BTN_SQUARE))
    {
        static const int standard[PSXS5_MAX_PADS] = {0, 1, 2, 3};
        plat_set_player_order(standard);
        S.players = false;
        app_toast("Players back in the console's order");
        sfx_play(SFX_SELECT);
        return;
    }
    if (pressed & BIT(BTN_CIRCLE))
    {
        S.players = false;
        sfx_play(SFX_BACK);
        return;
    }

    static const uint32_t colours[PSXS5_MAX_PADS] = {0xff2050ffu, 0xffff2030u, 0xff20d040u, 0xffff40c0u};
    const float w = 900, x = (plat_width() - w) * 0.5f, top = 250, row_h = 120;
    text_draw(plat_width() * 0.5f, 170, 30, FONT_BOLD, TH_TEXT, ALIGN_CENTER, tr("Player order"));
    draw_rrect(x - 16, top - 16, w + 32, PSXS5_MAX_PADS * row_h + 32, TH_RADIUS, TH_CARD);
    for (int i = 0; i < PSXS5_MAX_PADS; ++i)
    {
        float y = top + i * row_h;
        bool done = i < S.assigned, next = i == S.assigned;
        if (next)
            draw_rrect(x, y + 6, w, row_h - 12, TH_RADIUS_SMALL, TH_ROW_SELECTED);
        draw_circle(x + 56, y + row_h * 0.5f, 22, done || next ? colours[i] : argb_alpha(colours[i], 0.35f));
        char label[32];
        snprintf(label, sizeof(label), tr("Player %d"), i + 1);
        text_draw(x + 100, y + 26, 28, FONT_BOLD, done || next ? TH_TEXT : TH_TEXT_DIM, ALIGN_LEFT, label);
        const char *who = NULL;
        if (done)
            for (int k = 0; k < PSXS5_MAX_PADS; ++k)
                if (current[k] == S.order[i])
                    who = plat_pad_name(k);
        text_draw_fit(x + 100, y + 66, 22, FONT_REGULAR, next ? TH_FOCUS : TH_TEXT_SOFT, ALIGN_LEFT, w - 140,
                      done ? (who ? who : tr("Controller")) : next ? tr("Press Cross on this player's controller") : "");
    }
    static const int glyphs[] = {GLYPH_CROSS, GLYPH_SQUARE, GLYPH_CIRCLE};
    static const char *const labels[] = {"Choose", "Console's order", "Back"};
    app_draw_hints(glyphs, labels, 3, NULL);
}

/* ---------------------------------------------------------------- the profiles page */

/* The profiles, then a new one for each signed-in PS5 user who hasn't got
 * one, then a numbered player. */
static int who_rows(char adds[][PROFILE_NAME_LEN], int *add_count)
{
    *add_count = 0;
    for (int k = 0; k < PSXS5_MAX_PADS && *add_count < 5; ++k)
    {
        const char *name = app.pads[k].connected ? plat_pad_name(k) : NULL;
        if (!name || !name[0] || profiles_find(name) >= 0)
            continue;
        bool dup = false;
        for (int i = 0; i < *add_count; ++i)
            dup |= !strcmp(adds[i], name);
        if (!dup)
            str_copy(adds[(*add_count)++], PROFILE_NAME_LEN, name);
    }
    for (int n = 2; n < 20 && *add_count < 6; ++n)
    {
        char name[PROFILE_NAME_LEN];
        snprintf(name, sizeof(name), tr("Player %d"), n);
        if (profiles_find(name) < 0)
        {
            str_copy(adds[(*add_count)++], PROFILE_NAME_LEN, name);
            break;
        }
    }
    return profiles_count() + *add_count;
}

static void who_page(uint32_t pressed)
{
    char adds[6][PROFILE_NAME_LEN];
    int add_count, rows = who_rows(adds, &add_count), have = profiles_count();
    int before = S.who_cursor;
    if (pressed & BIT(BTN_UP))
        S.who_cursor = (S.who_cursor + rows - 1) % rows;
    if (pressed & BIT(BTN_DOWN))
        S.who_cursor = (S.who_cursor + 1) % rows;
    if (S.who_cursor >= rows)
        S.who_cursor = rows - 1;
    if (S.who_cursor != before)
        sfx_play(SFX_CLICK);
    if (pressed & BIT(BTN_CIRCLE))
    {
        S.who = false;
        sfx_play(SFX_BACK);
        return;
    }
    if (pressed & BIT(BTN_CROSS))
    {
        if (app.game)
            app_toast("Quit the game first");
        else
        {
            int target = S.who_cursor < have ? S.who_cursor : profiles_add(adds[S.who_cursor - have]);
            if (target >= 0 && profiles_switch(target))
            {
                char msg[96];
                snprintf(msg, sizeof(msg), tr("Playing as %s"), profiles_name(target));
                app_toast(msg);
                sfx_play(SFX_SELECT);
                S.who = false;
                return;
            }
        }
    }
    if ((pressed & BIT(BTN_SQUARE)) && S.who_cursor > 0 && S.who_cursor < have)
    {
        if (profiles_remove(S.who_cursor))
        {
            app_toast("Profile taken off the list (its files are kept)");
            S.who_cursor = 0;
        }
        else
            app_toast("Switch to another profile first");
    }

    const float w = 900, x = (plat_width() - w) * 0.5f, top = 230, row_h = 76;
    text_draw(plat_width() * 0.5f, 150, 30, FONT_BOLD, TH_TEXT, ALIGN_CENTER, tr("Who's playing"));
    draw_rrect(x - 16, top - 16, w + 32, rows * row_h + 24, TH_RADIUS, TH_CARD);
    float target_y = top + S.who_cursor * row_h;
    if (S.who_y == 0)
        S.who_y = target_y;
    anim_approach(&S.who_y, target_y, app.dt, TH_SNAP);
    draw_rrect(x, S.who_y, w, row_h - 8, TH_RADIUS_SMALL, TH_ROW_SELECTED);
    for (int i = 0; i < rows; ++i)
    {
        float y = top + i * row_h;
        bool add = i >= have, here = i == profiles_current();
        icon_draw(add ? ICON_PLUS : ICON_USER, x + 24, y + 18, 32, add ? TH_TEXT_DIM : TH_FOCUS);
        char label[PROFILE_NAME_LEN + 32];
        if (add)
            snprintf(label, sizeof(label), tr("New profile: %s"), adds[i - have]);
        else
            str_copy(label, sizeof(label), profiles_name(i));
        text_draw_fit(x + 76, y + 19, 26, here ? FONT_BOLD : FONT_REGULAR, add ? TH_TEXT_SOFT : TH_TEXT, ALIGN_LEFT,
                      w - 300, label);
        if (here)
            text_draw(x + w - 24, y + 22, 22, FONT_REGULAR, TH_GOOD, ALIGN_RIGHT, tr("Playing now"));
    }
    static const int glyphs[] = {GLYPH_CROSS, GLYPH_SQUARE, GLYPH_CIRCLE};
    static const char *const labels[] = {"Switch", "Remove", "Back"};
    app_draw_hints(glyphs, labels, 3, NULL);
}

/* ---------------------------------------------------------------- shared pieces */

#define CONTENT_X 372.0f
#define CONTENT_TOP 196.0f
#define CONTENT_BOTTOM 880.0f

static float content_w(void)
{
    return plat_width() - CONTENT_X - TH_MARGIN;
}

/* A card that lights up when the D-pad is on it. */
static void focus_card(float x, float y, float w, float h, float radius, bool sel, bool filled)
{
    if (sel)
    {
        draw_rrect(x, y, w, h, radius, TH_ROW_SELECTED);
        draw_rrect_outline(x, y, w, h, radius, 2.0f, argb_alpha(TH_FOCUS, 0.85f));
        draw_glow(x - 5, y - 5, w + 10, h + 10, 12, argb_alpha(TH_FOCUS, 0.09f));
    }
    else
    {
        draw_rrect(x, y, w, h, radius, filled ? TH_CARD : TH_CARD_A(0x88));
        draw_rrect_outline(x, y, w, h, radius, 1.0f, TH_DIVIDER);
    }
}

static float row_fraction(const Row *r)
{
    if (r->kind == K_TOGGLE)
        return read_value(r) ? 1.0f : 0.0f;
    if (r->kind != K_CHOICE || r->count < 2)
        return 0.0f;
    int v = read_value(r);
    if (v < 0)
        v = 0;
    if (v >= r->count)
        v = r->count - 1;
    return (float)v / (float)(r->count - 1);
}

static void draw_meter(float x, float y, float w, float h, float t, uint32_t colour)
{
    draw_rrect(x, y, w, h, h * 0.5f, TH_PILL);
    if (t > 0.01f)
        draw_rrect(x, y, w * t, h, h * 0.5f, colour);
}

/* The one-line "what this does" strip, different from the old help panel.
 * On the rail it names the tab; inside a tab it describes the chosen row. */
static void draw_caption(const char *name, const char *help, bool changing)
{
    const float x = CONTENT_X;
    float w = content_w();
    draw_rect(x, 890, w, 1, TH_DIVIDER);
    if (!name)
        return;
    if (changing)
    {
        const char *tag = tr("Changing");
        float tw = text_width(17, FONT_BOLD, tag) + 34;
        draw_rrect(x + w - tw, 893, tw, 34, 17, argb_alpha(TH_FOCUS, 0.16f));
        text_draw(x + w - tw * 0.5f, 900, 17, FONT_BOLD, TH_FOCUS, ALIGN_CENTER, tag);
        w -= tw + 18;
    }
    text_draw(x, 904, 23, FONT_BOLD, TH_TEXT, ALIGN_LEFT, name);
    float name_w = text_width(23, FONT_BOLD, name) + 26;
    if (help)
        text_draw_fit(x + name_w, 907, 20, FONT_REGULAR, TH_TEXT_DIM, ALIGN_LEFT, w - name_w, help);
}

/* A brighter ring around the row being changed, so editing reads at a glance. */
static void draw_edit_frame(void)
{
    if (!S.editing || S.cursor < 0 || S.cursor >= S.row_rects || S.row_w[S.cursor] <= 0)
        return;
    /* the palette marks the chosen card itself; the row rect only covers half
     * of the strip so Down lands on Accent */
    if (tab()->rows == LIBRARY && S.cursor == 0)
        return;
    float pulse = 0.55f + 0.45f * sinf((float)plat_ticks_us() / 1e6f * 5.0f);
    draw_rrect_outline(S.row_x[S.cursor] - 3, S.row_y[S.cursor] - 3, S.row_w[S.cursor] + 6,
                       S.row_h[S.cursor] + 6, TH_RADIUS_SMALL + 3, 3.0f,
                       argb_alpha(TH_FOCUS, 0.45f + 0.45f * pulse));
}

/* ---------------------------------------------------------------- the rail */

static void draw_rail(void)
{
    const float x = TH_MARGIN, w = 264, h = 72, gap = 12;
    const float target = CONTENT_TOP + S.tab * (h + gap);
    if (S.rail_y < 0)
        S.rail_y = target;
    anim_approach(&S.rail_y, target, app.dt, TH_SNAP);
    draw_rrect(x, S.rail_y, w, h, TH_RADIUS_SMALL, TH_ROW_SELECTED);
    draw_rrect(x, S.rail_y + 16, 4, h - 32, 2, TH_FOCUS);
    for (int i = 0; i < TAB_COUNT; ++i)
    {
        const float y = CONTENT_TOP + i * (h + gap);
        bool on = i == S.tab;
        icon_draw(TABS[i].icon, x + 26, y + 22, 28, on ? TH_FOCUS : TH_TEXT_DIM);
        text_draw(x + 74, y + 12, 25, on ? FONT_BOLD : FONT_REGULAR, on ? TH_TEXT : TH_TEXT_DIM,
                  ALIGN_LEFT, tr(TABS[i].name));
        char num[8];
        snprintf(num, sizeof(num), "%02d", i + 1);
        text_draw(x + w - 18, y + 26, 16, FONT_MONO, on ? TH_FOCUS : argb_alpha(TH_TEXT_DIM, 0.55f),
                  ALIGN_RIGHT, num);
    }
    /* the footer: the version, and how the tabs move */
    text_draw(TH_MARGIN, 922, 18, FONT_MONO, TH_TEXT_DIM, ALIGN_LEFT, "GBAR5 " PSXS5_VERSION);
    draw_pad_glyph(GLYPH_UP, TH_MARGIN + 190, 946, 20);
    draw_pad_glyph(GLYPH_DOWN, TH_MARGIN + 190, 972, 20);
    text_draw(TH_MARGIN + 226, 945, 15, FONT_MONO, TH_TEXT_DIM, ALIGN_LEFT, tr("Tabs"));
    /* the rail has the focus: a ring around the chosen tab */
    if (S.rail && S.action < 0)
    {
        draw_rrect_outline(x - 2, S.rail_y - 2, w + 4, h + 4, TH_RADIUS_SMALL + 2, 2.0f,
                           argb_alpha(TH_FOCUS, 0.9f));
        draw_glow(x - 6, S.rail_y - 6, w + 12, h + 12, 16, argb_alpha(TH_FOCUS, 0.10f));
    }
}

/* ---------------------------------------------------------------- Screen: a picture lab */

static void aspect_box(int aspect, float bx, float by, float bw, float bh, float *ox, float *oy,
                       float *ow, float *oh)
{
    if (aspect == ASPECT_STRETCH)
    {
        *ox = bx, *oy = by, *ow = bw, *oh = bh;
        return;
    }
    float ar = 1.5f;
    if (aspect == ASPECT_4_3)
        ar = 4.0f / 3.0f;
    else if (aspect == ASPECT_16_9)
        ar = 16.0f / 9.0f;
    else if (aspect == ASPECT_16_10)
        ar = 16.0f / 10.0f;
    else if (aspect == ASPECT_PIXEL)
        ar = 1.0f;
    float w = bw, h = w / ar;
    if (h > bh)
    {
        h = bh;
        w = h * ar;
    }
    *ox = bx + (bw - w) * 0.5f;
    *oy = by + (bh - h) * 0.5f;
    *ow = w;
    *oh = h;
}

/* The 240 x 160 picture, drawn as a pixel sunset so the TV frame is not empty. */
static void draw_gba_scene(float x, float y, float w, float h, int brightness, int colour, int grid)
{
    plat_set_clip((int)x - 1, (int)y - 1, (int)w + 2, (int)h + 2);
    uint32_t sky[2] = {argb_lerp(TH_BG_DEEP, TH_FOCUS, 0.30f), argb_lerp(TH_BG_DEEP, TH_GOLD, 0.20f)};
    float stops[2] = {0.0f, 1.0f};
    draw_vgradient(x, y, w, h * 0.66f, sky, stops, 2);
    /* the sun and its halo */
    draw_circle(x + w * 0.74f, y + h * 0.28f, h * 0.17f, argb_alpha(TH_GOLD, 0.16f));
    draw_circle(x + w * 0.74f, y + h * 0.28f, h * 0.11f, TH_GOLD);
    /* hills, cut off by the screen's edge */
    draw_circle(x + w * 0.16f, y + h * 0.82f, w * 0.30f, argb_lerp(TH_BG_DEEP, TH_FOCUS, 0.10f));
    draw_circle(x + w * 0.52f, y + h * 0.90f, w * 0.36f, argb_lerp(TH_BG_DEEP, TH_FOCUS, 0.16f));
    draw_rect(x, y + h * 0.70f, w, h * 0.30f, argb_lerp(TH_BG_DEEP, TH_FOCUS, 0.06f));
    /* a few pixel clouds */
    for (int i = 0; i < 3; ++i)
    {
        float cx = x + w * (0.12f + i * 0.28f), cy = y + h * (0.12f + (i % 2) * 0.10f);
        draw_rrect(cx, cy, w * 0.16f, h * 0.045f, h * 0.022f, argb_alpha(TH_TEXT, 0.10f));
        draw_rrect(cx + w * 0.03f, cy - h * 0.035f, w * 0.09f, h * 0.045f, h * 0.022f,
                   argb_alpha(TH_TEXT, 0.08f));
    }
    /* a tiny player on the hill */
    draw_rrect(x + w * 0.20f, y + h * 0.56f, w * 0.045f, h * 0.115f, 2, TH_TEXT_SOFT);
    draw_circle(x + w * 0.2225f, y + h * 0.535f, h * 0.035f, TH_TEXT);
    /* the GBA's own rows of pixels */
    if (grid > 0)
    {
        uint32_t line = argb_alpha(TH_BG_DEEP, grid == 1 ? 0.16f : 0.30f);
        for (float yy = y; yy < y + h; yy += 3.0f)
            draw_rect(x, yy, w, 1, line);
        for (float xx = x; xx < x + w; xx += 3.0f)
            draw_rect(xx, y, 1, h, line);
    }
    /* brightness and colour, as the settings would */
    static const float dims[] = {0.20f, 0.0f, 0.10f, 0.20f};
    if (brightness >= 0 && brightness < 4 && dims[brightness] > 0.0f)
        draw_rect(x, y, w, h, brightness == 0 ? argb_alpha(0xff000000u, dims[brightness])
                                              : argb_alpha(0xffffffffu, dims[brightness]));
    if (colour == 1)
        draw_rect(x, y, w, h, argb_alpha(TH_FOCUS, 0.10f));
    else if (colour == 3)
        draw_rect(x, y, w, h, argb_alpha(TH_GOLD, 0.10f));
    else if (colour == 4)
        draw_rect(x, y, w, h, argb_alpha(TH_TEXT_SOFT, 0.07f));
    else if (colour == 5)
        draw_rect(x, y, w, h, argb_alpha(0xffb9c4bau, 0.55f));
    plat_set_clip(0, 0, 0, 0);
}

static void draw_screen_tab(void)
{
    const float x = CONTENT_X, w = content_w(), hero_h = 368;
    draw_rrect(x, CONTENT_TOP, w, hero_h, TH_RADIUS, TH_CARD_A(0xc8));
    draw_rrect_outline(x, CONTENT_TOP, w, hero_h, TH_RADIUS, 1, TH_DIVIDER);
    text_draw(x + 30, CONTENT_TOP + 20, 16, FONT_MONO, TH_TEXT_DIM, ALIGN_LEFT, "PREVIEW");
    text_draw(x + w - 30, CONTENT_TOP + 20, 16, FONT_MONO, TH_FOCUS, ALIGN_RIGHT,
              tr("Live picture"));

    int aspect = read_value(&DISPLAY[0]);
    float bx = x + 44, by = CONTENT_TOP + 52, bw = w - 88, bh = hero_h - 96;
    float sx, sy, sw, sh;
    aspect_box(aspect, bx, by, bw, bh, &sx, &sy, &sw, &sh);
    draw_rrect(sx - 10, sy - 10, sw + 20, sh + 20, 10, TH_BG_DEEP);
    draw_gba_scene(sx, sy, sw, sh, read_value(&DISPLAY[8]), read_value(&DISPLAY[9]),
                   read_value(&DISPLAY[7]));
    draw_rrect_outline(sx - 10, sy - 10, sw + 20, sh + 20, 10, 1, argb_alpha(TH_FOCUS, 0.35f));

    char abuf[160], vbuf[160];
    const char *aspect_name = value_label(&DISPLAY[0], vbuf, sizeof(vbuf));
    snprintf(abuf, sizeof(abuf), "%s  \xc2\xb7  240 x 160", aspect_name);
    text_draw(sx + sw * 0.5f, sy + sh + 18, 18, FONT_MONO, TH_TEXT_DIM, ALIGN_CENTER, abuf);

    /* the empty margins carry the two numbers the picture is about */
    if (sx - bx > 190)
    {
        int up = read_value(&DISPLAY[1]);
        int factor = up > 0 ? up + 1 : 1;
        char shape[32], pixels[40], filter[80];
        static const char *const SHAPES[] = {"3:2", "4:3", "16:9", "16:10", "1:1", "Fill"};
        snprintf(shape, sizeof(shape), "%s", SHAPES[aspect >= 0 && aspect < 6 ? aspect : 0]);
        snprintf(pixels, sizeof(pixels), "%d x %d", 240 * factor, 160 * factor);
        snprintf(filter, sizeof(filter), "%s", value_label(&DISPLAY[2], vbuf, sizeof(vbuf)));
        float mid = CONTENT_TOP + hero_h * 0.5f;
        text_draw(bx + 46, mid - 52, 44, FONT_BOLD, argb_alpha(TH_FOCUS, 0.55f), ALIGN_LEFT, shape);
        text_draw(bx + 48, mid + 8, 15, FONT_MONO, TH_TEXT_DIM, ALIGN_LEFT, tr("Picture shape"));
        text_draw(x + w - 48, mid - 52, 30, FONT_BOLD, argb_alpha(TH_TEXT, 0.75f), ALIGN_RIGHT, pixels);
        text_draw(x + w - 48, mid - 8, 15, FONT_MONO, TH_TEXT_DIM, ALIGN_RIGHT, tr("Upscaled"));
        text_draw_fit(x + w - 48 - 240, mid + 20, 17, FONT_MONO, argb_alpha(TH_FOCUS, 0.7f),
                      ALIGN_RIGHT, 240, filter);
    }

    /* the twelve rows as two shelves of tiles */
    const int cols = 6;
    const float gap = 12, tw = (w - gap * (cols - 1)) / cols, th = 132,
                ty0 = CONTENT_TOP + hero_h + 22;
    for (int i = 0; i < DISPLAY_COUNT; ++i)
    {
        const Row *r = &DISPLAY[i];
        float tx = x + (i % cols) * (tw + gap), ty = ty0 + (i / cols) * (th + gap);
        note_row(i, tx, ty, tw, th);
        bool sel = row_focused(i);
        focus_card(tx, ty, tw, th, TH_RADIUS_SMALL, sel, true);
        char n[8];
        snprintf(n, sizeof(n), "%02d", i + 1);
        text_draw(tx + 14, ty + 12, 14, FONT_MONO, argb_alpha(TH_TEXT_DIM, 0.7f), ALIGN_LEFT, n);
        text_draw_fit(tx + 16, ty + 34, 19, FONT_REGULAR, sel ? TH_TEXT : TH_TEXT_SOFT, ALIGN_LEFT,
                      tw - 32, tr(r->name));
        char buf[160];
        const char *v = value_label(r, buf, sizeof(buf));
        text_draw_fit(tx + 16, ty + 70, 22, FONT_BOLD, sel ? TH_FOCUS : TH_TEXT, ALIGN_LEFT,
                      tw - 32, v);
        draw_meter(tx + 16, ty + th - 22, tw - 32, 6, row_fraction(r),
                   sel ? TH_FOCUS : argb_alpha(TH_FOCUS, 0.45f));
    }
}

/* ---------------------------------------------------------------- Controls: the pad stage */

static void draw_controls_tab(void)
{
    const float x = CONTENT_X, w = content_w();
    float cx = x + 392, cy = 520;
    draw_glow(x + 40, cy - 260, 720, 540, 70, argb_alpha(TH_FOCUS, 0.05f));
    draw_circle(cx, cy + 40, 250, TH_BG_A(0x38));
    draw_controller(cx, cy, -1);

    /* the settings as keys beside the pad */
    const float kx = x + 800, kw = w - 800, kh = 60, kgap = 8;
    const char *buf;
    char value[160];
    for (int i = 0; i < CONTROLS_COUNT; ++i)
    {
        const Row *r = &CONTROLS[i];
        float y = CONTENT_TOP + i * (kh + kgap);
        note_row(i, kx, y, kw, kh);
        bool sel = row_focused(i);
        focus_card(kx, y, kw, kh, TH_RADIUS_SMALL, sel, true);
        draw_rrect(kx, y + 12, 3, kh - 24, 1.5f, sel ? TH_FOCUS : TH_DIVIDER);
        text_draw_fit(kx + 18, y + 10, 20, FONT_REGULAR, sel ? TH_TEXT : TH_TEXT_DIM, ALIGN_LEFT,
                      kw - 210, tr(r->name));
        buf = value_label(r, value, sizeof(value));
        if (!buf[0])
            buf = tr(r->kind == K_ACTION ? "Open" : "Hold the touchpad");
        if (r->kind == K_TOGGLE)
        {
            float *st = &S.switch_t[i < 16 ? i : 15];
            anim_approach(st, (float)read_value(r), app.dt, TH_SNAP);
            draw_switch(kx + kw - 106, y + 20, 20, *st);
        }
        else if (r->kind == K_ACTION)
            icon_draw(ICON_CHEVRON_RIGHT, kx + kw - 34, y + 22, 22, TH_TEXT_SOFT);
        else if (r->kind == K_INFO)
            text_draw_fit(kx + 18, y + 34, 17, FONT_MONO, TH_TEXT_DIM, ALIGN_LEFT, kw - 40, buf);
        else
        {
            text_draw_fit(kx + 18, y + 34, 18, FONT_BOLD, sel ? TH_FOCUS : TH_TEXT_SOFT, ALIGN_LEFT,
                          kw - 40, buf);
            icon_draw(ICON_CHEVRON_RIGHT, kx + kw - 34, y + 20, 20,
                      argb_alpha(TH_TEXT_SOFT, sel ? 0.9f : 0.45f));
        }
    }
}


/* A QR asset (assets/qr-*.png, one pixel per module, made with segno: see
 * tools/make_qr.py). Drawn on a white card so phones read it on any
 * background. Each caller keeps its own cache. */
typedef struct
{
    PlatTexture *tex;
    int modules;
    bool tried;
} QrAsset;

/* Returns the drawn side in pixels (a whole number of modules, so often less
 * than the nominal size), or 0.0f when there was nothing to draw. The card's
 * visual centre is x + side * 0.5f, not x + size * 0.5f. */
static float draw_asset_qr(QrAsset *a, const char *asset, float x, float y, float size)
{
    if (!a->tried)
    {
        a->tried = true;
        char path[PSXS5_PATH_MAX];
        plat_asset_path(path, sizeof(path), asset);
        int w = 0, h = 0, comp = 0;
        unsigned char *px = stbi_load(path, &w, &h, &comp, 4);
        if (px)
        {
            a->tex = plat_texture_create(px, w, h, false);
            a->modules = w;
            stbi_image_free(px);
        }
    }
    if (!a->tex || a->modules <= 0)
        return 0.0f;
    int scale = (int)(size / a->modules);
    float side = (float)(scale * a->modules), pad = 14;
    draw_rrect(x - pad, y - pad, side + 2 * pad, side + 2 * pad, TH_RADIUS_SMALL, 0xffffffffu);
    plat_draw_texture(a->tex, x, y, side, side, 0xffffffffu, false);
    return side;
}

/* The project page, and the tip jar beside it. */
static QrAsset qr_project, qr_kofi;


/* Any text as a QR code (the phone page's address), made on the console.
 * The address carries the console's 128-bit code, so it is longer than a bare
 * URL: low error correction keeps the code at 33 modules, which the small
 * card can still draw at 4 pixels a module. A clean screen is all it needs. */
static float draw_url_qr(const char *text, float x, float y, float size)
{
    static char made_for[160];
    static PlatTexture *tex;
    static int modules;
    if (strcmp(made_for, text) != 0)
    {
        str_copy(made_for, sizeof(made_for), text);
        plat_texture_free(tex);
        tex = NULL;
        static uint8_t qr[qrcodegen_BUFFER_LEN_MAX], temp[qrcodegen_BUFFER_LEN_MAX];
        if (qrcodegen_encodeText(text, temp, qr, qrcodegen_Ecc_LOW, qrcodegen_VERSION_MIN,
                                 qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true))
        {
            int n = qrcodegen_getSize(qr), side = n + 4;
            uint8_t *px = malloc((size_t)side * side * 4);
            if (px)
            {
                for (int j = 0; j < side; ++j)
                    for (int i = 0; i < side; ++i)
                    {
                        bool dark = qrcodegen_getModule(qr, i - 2, j - 2);
                        uint8_t *p = &px[(j * side + i) * 4];
                        p[0] = dark ? 15 : 255, p[1] = dark ? 19 : 255, p[2] = dark ? 48 : 255, p[3] = 255;
                    }
                tex = plat_texture_create(px, side, side, false);
                modules = side;
                free(px);
            }
        }
    }
    if (!tex)
        return 0.0f;
    int scale = (int)(size / modules);
    float side = (float)(scale * modules), pad = 14;
    draw_rrect(x - pad, y - pad, side + 2 * pad, side + 2 * pad, TH_RADIUS_SMALL, 0xffffffffu);
    plat_draw_texture(tex, x, y, side, side, 0xffffffffu, false);
    return side;
}

/* ---------------------------------------------------------------- Sound: the mixer */

static void draw_sound_tab(void)
{
    const float x = CONTENT_X, w = content_w(), top = CONTENT_TOP + 10, h = 520;
    const float chan_w = 236, gap = 26;
    for (int i = 0; i < SOUND_COUNT; ++i)
    {
        const Row *r = &SOUND[i];
        bool sel = row_focused(i);
        float cxx = x + i * (chan_w + gap);
        note_row(i, cxx, top, chan_w, h);
        focus_card(cxx, top, chan_w, h, TH_RADIUS, sel, true);
        text_draw_fit(cxx + 20, top + 20, 20, FONT_BOLD, sel ? TH_TEXT : TH_TEXT_DIM, ALIGN_LEFT,
                      chan_w - 40, tr(r->name));
        char buf[96];
        text_draw_fit(cxx + 20, top + 50, 26, FONT_BOLD, sel ? TH_FOCUS : TH_TEXT, ALIGN_LEFT,
                      chan_w - 40, value_label(r, buf, sizeof(buf)));
        /* the fader */
        float t = row_fraction(r);
        float tx = cxx + chan_w * 0.5f - 9, ty = top + 120, th = 290;
        draw_rrect(tx, ty, 18, th, 9, TH_PILL);
        draw_rrect(tx, ty + th * (1 - t), 18, th * t, 9, sel ? TH_FOCUS : argb_alpha(TH_FOCUS, 0.55f));
        for (int k = 0; k <= 4; ++k)
            draw_rect(tx - 10, ty + th * k / 4.0f, 8, 1, TH_DIVIDER);
        float knob = ty + th * (1 - t);
        draw_rrect(cxx + chan_w * 0.5f - 46, knob - 14, 92, 28, 14, TH_TEXT);
        draw_rrect(cxx + chan_w * 0.5f - 46, knob - 14, 92, 28, 14,
                   sel ? TH_FOCUS : TH_TEXT_SOFT);
        text_draw(cxx + chan_w - 20, top + h - 44, 15, FONT_MONO, TH_TEXT_DIM, ALIGN_RIGHT,
                  t >= 0.999f ? "MAX" : "");
    }

    /* the analyser, so the page looks alive */
    float ax = x + 3 * (chan_w + gap) + 8, aw = x + w - ax;
    draw_rrect(ax, top, aw, h, TH_RADIUS, TH_CARD_A(0xa0));
    draw_rrect_outline(ax, top, aw, h, TH_RADIUS, 1, TH_DIVIDER);
    text_draw(ax + 22, top + 18, 16, FONT_MONO, TH_TEXT_DIM, ALIGN_LEFT, "44.1 kHz");
    float base = top + h - 60, bw = (aw - 60) / 26.0f;
    float now = (float)plat_ticks_us() / 1e6f;
    for (int i = 0; i < 26; ++i)
    {
        float amp = 0.28f + 0.22f * sinf(now * 2.2f + i * 0.55f) +
                    0.14f * sinf(now * 3.7f - i * 0.31f);
        if (amp < 0.05f)
            amp = 0.05f;
        float bh = 60 + amp * 300.0f;
        float bx = ax + 30 + i * bw;
        draw_rrect(bx, base - bh, bw - 6, bh, (bw - 6) * 0.5f,
                   i % 5 == 0 ? TH_FOCUS : argb_alpha(TH_FOCUS, 0.35f + 0.4f * amp));
        draw_rrect(bx, base - bh, bw - 6, 4, 2, argb_alpha(TH_TEXT, 0.35f));
    }
    draw_rect(ax + 26, base + 10, aw - 52, 1, TH_DIVIDER);
    text_draw(ax + aw * 0.5f, base + 24, 18, FONT_REGULAR, TH_TEXT_DIM, ALIGN_CENTER,
              tr("Audio filter preview"));
}

/* ---------------------------------------------------------------- Shelf: the palette and your own colours */

/* One palette card: a tiny shelf in that theme, its name, and a dot when it
 * is the one in use. */
static void draw_theme_card(int id, float x, float y, float w, float h, bool current, bool sel)
{
    const Theme *p = theme_at(id);
    draw_rrect(x, y, w, h, TH_RADIUS, p->bg);
    draw_rrect_outline(x, y, w, h, TH_RADIUS, sel ? 3.0f : 1.0f,
                       sel ? p->focus : (current ? argb_alpha(p->focus, 0.55f) : TH_DIVIDER));
    if (sel)
        draw_glow(x - 5, y - 5, w + 10, h + 10, 14, argb_alpha(p->focus, 0.16f));

    const float sx = x + 10, sy = y + 10, sw = w - 20, sh = h - 46;
    draw_rrect(sx, sy, sw, sh, 9, p->bg_deep);
    const float cw = (sw - 24) / 3.0f;
    for (int i = 0; i < 3; ++i)
    {
        float cx = sx + 6 + i * (cw + 6);
        draw_rrect(cx, sy + 10, cw, sh - 24, 5, i == 0 ? p->row_selected : p->card);
        draw_rrect(cx + 4, sy + 16, cw - 8, 17, 3, argb_alpha(p->focus, i == 0 ? 0.9f : 0.32f));
        draw_rrect(cx + 4, sy + 40, (cw - 8) * 0.62f, 4, 2, argb_alpha(p->text, 0.35f));
        draw_rrect(cx + 4, sy + 49, (cw - 8) * 0.44f, 4, 2, argb_alpha(p->text, 0.22f));
    }
    /* the accent, as a little strip */
    draw_rrect(sx + 6, sy + sh - 11, 28, 6, 3, p->focus);
    draw_rrect(sx + 40, sy + sh - 11, 15, 6, 3, argb_alpha(p->focus, 0.45f));

    text_draw_fit(x + 12, y + h - 28, 18, FONT_BOLD, p->text, ALIGN_LEFT, w - 46, THEME_NAMES[id]);
    if (current)
    {
        draw_circle(x + w - 15, y + h - 19, 5, p->focus);
        draw_circle(x + w - 15, y + h - 19, 2, p->bg);
    }
}

/* One #hex field: a swatch, its name, and the six digits in their own pill,
 * with the chosen digit lit while the D-pad is on it. */
static void draw_colour_field(const Row *r, float x, float y, float w, float h, bool sel,
                              bool editing)
{
    const uint32_t c = colour_of(r);
    char hex[8];
    theme_hex(c, hex, sizeof(hex));
    const bool active = app.global.theme == THEME_CUSTOM;
    const Theme *p = theme_at(THEME_CUSTOM);

    draw_rrect(x, y, w, h, TH_RADIUS_SMALL, active ? TH_CARD : TH_CARD_A(0x70));
    draw_rrect_outline(x, y, w, h, TH_RADIUS_SMALL, sel ? 2.5f : 1.0f,
                       sel ? TH_FOCUS : active ? argb_alpha(TH_FOCUS, 0.35f) : TH_DIVIDER);
    if (sel)
        draw_glow(x - 5, y - 5, w + 10, h + 10, 12, argb_alpha(TH_FOCUS, 0.10f));

    const float sw = h - 26;
    draw_rrect(x + 13, y + 13, sw, sw, 9, 0xff000000u | c);
    draw_rrect_outline(x + 13, y + 13, sw, sw, 9, 1, argb_alpha(TH_TEXT, 0.22f));
    text_draw(x + 28 + sw, y + 13, 15, FONT_MONO, TH_TEXT_DIM, ALIGN_LEFT, tr(r->name));

    const float cell = 30, by = y + 36;
    float bx = x + 28 + sw;
    draw_rrect(bx - 7, by, 18 + 6 * cell + 7, 42, 10,
               argb_alpha(TH_BG_DEEP, active ? 0.62f : 0.38f));
    text_draw(bx, by + 10, 22, FONT_MONO, TH_TEXT_DIM, ALIGN_LEFT, "#");
    for (int i = 0; i < 6; ++i)
    {
        bool on = editing && i == S.hex_pos;
        float cx = bx + 18 + i * cell;
        if (on)
            draw_rrect(cx, by - 2, cell, 46, 8, p->focus);
        char ch[2] = {hex[i + 1], 0};
        text_draw(cx + cell * 0.5f, by + 8, 25, FONT_MONO, on ? p->bg : TH_TEXT, ALIGN_CENTER, ch);
    }
    if (active)
        text_draw(x + w - 14, y + h - 22, 13, FONT_MONO, argb_alpha(TH_FOCUS, 0.85f), ALIGN_RIGHT,
                  tr("In use"));
    else
        text_draw(x + w - 14, y + h - 22, 13, FONT_MONO, TH_TEXT_DIM, ALIGN_RIGHT, tr("Custom"));
}

/* What the two colours look like together: the card, the row and the shelf. */
static void draw_custom_preview(float x, float y, float w, float h, bool active)
{
    const Theme *p = theme_at(THEME_CUSTOM);
    draw_rrect(x, y, w, h, TH_RADIUS_SMALL, p->bg);
    draw_rrect_outline(x, y, w, h, TH_RADIUS_SMALL, 1,
                       argb_alpha(p->focus, active ? 0.6f : 0.22f));
    const float card = h - 34;
    draw_rrect(x + 16, y + 17, card, card, 8, p->card);
    draw_rrect(x + 16, y + 17, card, 5, 2, p->focus);
    draw_circle(x + 16 + card - 18, y + 17 + card - 18, 8, argb_alpha(p->gold, 0.85f));
    float sx = x + card + 34;
    for (int i = 0; i < 5; ++i)
    {
        float t = sx + i * 88;
        draw_rrect(t, y + 23, 72, h - 60, 6, i == 0 ? p->row_selected : p->card);
        draw_rrect(t + 8, y + 33, 56, 24, 3, argb_alpha(p->focus, i == 0 ? 0.9f : 0.35f));
        draw_rrect(t + 8, y + 63, 36, 4, 2, argb_alpha(p->text, 0.35f));
    }
    char a[8], b[8], line[40];
    theme_hex(theme_custom_bg(), a, sizeof(a));
    theme_hex(theme_custom_accent(), b, sizeof(b));
    snprintf(line, sizeof(line), "%s  \xc2\xb7  %s", a, b);
    text_draw(sx, y + h - 26, 14, FONT_MONO, argb_alpha(p->text, 0.72f), ALIGN_LEFT, line);
    if (active)
        text_draw(x + w - 16, y + h - 26, 13, FONT_MONO, p->focus, ALIGN_RIGHT, tr("In use"));
    else
    {
        draw_rect(x, y, w, h, 0x72000000u);
        const char *msg = tr("Select Custom to use these");
        float tw = text_width(16, FONT_MONO, msg) + 34;
        draw_rrect(x + (w - tw) * 0.5f, y + h * 0.5f - 21, tw, 42, 21, TH_PILL_A(0xf0));
        text_draw(x + w * 0.5f, y + h * 0.5f - 9, 16, FONT_MONO, TH_TEXT, ALIGN_CENTER, msg);
    }
}

static void draw_shelf_tab(void)
{
    const float x = CONTENT_X, w = content_w();
    const int current = read_value(&LIBRARY[0]);
    const float gap = 8, card_h = 148;
    const float card_w = (w - gap * (THEME_COUNT - 1)) / THEME_COUNT;

    /* the ten presets and Custom across the top. The row's recorded rect is only the
     * left half: Down from it should land on Accent, not on the field that
     * happens to sit closest to the middle of the whole strip. */
    note_row(0, x, CONTENT_TOP, w * 0.5f, card_h);
    bool theme_sel = row_focused(0);
    for (int i = 0; i < THEME_COUNT; ++i)
        draw_theme_card(i, x + i * (card_w + gap), CONTENT_TOP, card_w, card_h, i == current,
                        theme_sel && i == current);
    char in_use[80];
    snprintf(in_use, sizeof(in_use), tr("In use: %s"), THEME_NAMES[current]);
    text_draw(x, CONTENT_TOP + card_h + 8, 14, FONT_MONO, TH_TEXT_DIM, ALIGN_LEFT, in_use);

    /* the Custom bar: two hex codes and a live preview */
    const float bar_y = CONTENT_TOP + card_h + 30, bar_h = 180;
    draw_rrect(x, bar_y, w, bar_h, TH_RADIUS, TH_CARD);
    draw_rrect_outline(x, bar_y, w, bar_h, TH_RADIUS, 1,
                       current == THEME_CUSTOM ? argb_alpha(TH_FOCUS, 0.45f) : TH_DIVIDER);
    text_draw(x + 22, bar_y + 16, 20, FONT_BOLD, TH_TEXT, ALIGN_LEFT, tr("Your own colours"));
    text_draw_fit(x + 22, bar_y + 44, 15, FONT_REGULAR, TH_TEXT_DIM, ALIGN_LEFT, 760,
                  tr("Any #hex code at all. Dark grounds get light text; light grounds get dark text."));
    const float fw = 380, fh = 82, fy = bar_y + 80;
    note_row(1, x + 22, fy, fw, fh);
    note_row(2, x + 22 + fw + 12, fy, fw, fh);
    draw_colour_field(&LIBRARY[1], x + 22, fy, fw, fh, row_focused(1),
                      S.editing && S.cursor == 1);
    draw_colour_field(&LIBRARY[2], x + 22 + fw + 12, fy, fw, fh, row_focused(2),
                      S.editing && S.cursor == 2);
    draw_custom_preview(x + 22 + 2 * fw + 24, bar_y + 16, w - (2 * fw + 46) - 22, bar_h - 32,
                        current == THEME_CUSTOM);

    /* the rest as seven tiles, four across */
    const float tth = 106, tgap = 12, ttw = (w - 3 * tgap) / 4;
    for (int i = 3; i < LIBRARY_COUNT; ++i)
    {
        const Row *r = &LIBRARY[i];
        int slot = i - 3, col = slot % 4, line = slot / 4;
        float tx = x + col * (ttw + tgap), ty = bar_y + bar_h + 16 + line * (tth + tgap);
        note_row(i, tx, ty, ttw, tth);
        bool sel = row_focused(i);
        focus_card(tx, ty, ttw, tth, TH_RADIUS_SMALL, sel, true);
        static const int icons[] = {ICON_PALETTE, ICON_SPARKLES, ICON_PALETTE, ICON_PHOTO,
                                    ICON_CARDS, ICON_DOWNLOAD, ICON_WORLD, ICON_ARROWS_SORT,
                                    ICON_CATEGORY, ICON_REFRESH};
        icon_draw(icons[i], tx + 18, ty + 18, 22, sel ? TH_FOCUS : TH_TEXT_DIM);
        text_draw_fit(tx + 52, ty + 15, 18, FONT_REGULAR, sel ? TH_TEXT : TH_TEXT_SOFT, ALIGN_LEFT,
                      ttw - 70, tr(r->name));
        char buf[160];
        const char *v = value_label(r, buf, sizeof(buf));
        if (r->kind == K_TOGGLE)
            v = read_value(r) ? tr("On") : tr("Off");
        if (r->kind == K_ACTION)
            v = tr("Open");
        text_draw_fit(tx + 52, ty + 45, 19, FONT_BOLD, sel ? TH_FOCUS : TH_TEXT, ALIGN_LEFT,
                      ttw - 90, v);
        if (r->kind == K_CHOICE)
            draw_meter(tx + 18, ty + tth - 18, ttw - 36, 5, row_fraction(r),
                       sel ? TH_FOCUS : argb_alpha(TH_FOCUS, 0.4f));
    }
    text_draw(x + w, bar_y + bar_h + 16 + 2 * (tth + tgap) + 6, 14, FONT_MONO, TH_TEXT_DIM,
              ALIGN_RIGHT, tr("Changes preview live; Save keeps them"));
}

/* ---------------------------------------------------------------- System: the tile board */

static void draw_system_tab(void)
{
    const float x = CONTENT_X, w = content_w(), gap = 12, tw = (w - gap) * 0.5f, th = 84;
    float ys[SYSTEM_COUNT];
    int cols[SYSTEM_COUNT];
    float y = 0;
    int col = 0;
    /* where the phone card goes: right under the "Settings from your phone" row */
    int phone_row = -1;
    for (int i = 0; i < SYSTEM_COUNT; ++i)
        if (SYSTEM[i].special == SP_REMOTE)
            phone_row = i;
    const float phone_h = 252, phone_gap = 14;
    float card_y = 0;

    for (int i = 0; i < SYSTEM_COUNT; ++i)
    {
        if (SYSTEM[i].group)
        {
            if (col)
            {
                y += th + gap;
                col = 0;
            }
            y += 32;
        }
        ys[i] = y;
        cols[i] = col;
        if (++col == 2)
        {
            col = 0;
            y += th + gap;
        }
        if (i == phone_row)
        {
            /* a full-width card under that line, with the code on it */
            if (col)
            {
                col = 0;
                y += th + gap;
            }
            card_y = y;
            y += phone_h + phone_gap;
        }
    }
    if (col)
        y += th + gap;

    float view = CONTENT_BOTTOM - CONTENT_TOP, want = S.scroll;
    if (S.cursor >= 0)
    {
        float sel_y = ys[S.cursor];
        if (sel_y - want < 28)
            want = sel_y - 28;
        if (sel_y + th - want > view - 8)
            want = sel_y + th - view + 8;
    }
    if (want > y - view)
        want = y - view;
    if (want < 0)
        want = 0;
    anim_approach(&S.scroll, want, app.dt, TH_SNAP);

    plat_set_clip((int)x - 8, (int)CONTENT_TOP - 8, (int)w + 16, (int)view + 16);
    for (int i = 0; i < SYSTEM_COUNT; ++i)
    {
        const Row *r = &SYSTEM[i];
        float ry = CONTENT_TOP + ys[i] - S.scroll;
        if (r->group)
            text_draw(x + 4, ry - 30, 16, FONT_BOLD, argb_alpha(TH_TEXT_DIM, 0.8f), ALIGN_LEFT,
                      tr(r->group));
        float tx = x + cols[i] * (tw + gap);
        note_row(i, tx, ry, tw, th);
        if (ry > CONTENT_BOTTOM || ry + th < CONTENT_TOP)
            continue;
        bool sel = row_focused(i);
        focus_card(tx, ry, tw, th, TH_RADIUS_SMALL, sel, true);
        draw_rrect(tx, ry + 14, 3, th - 28, 1.5f, sel ? TH_FOCUS : TH_DIVIDER);
        char buf[160];
        const char *v = value_label(r, buf, sizeof(buf));
        if (r->kind == K_ACTION)
            v = tr("Open");
        else if (!v[0])
            v = "";
        /* a long label and a long value: the value moves under the label.
         * The value ends at 42 % of the tile, so the label must stop before it. */
        bool stacked = r->kind != K_TOGGLE && r->kind != K_ACTION &&
                       text_width(20, FONT_REGULAR, tr(r->name)) +
                               text_width(19, FONT_REGULAR, v) >
                           tw * 0.42f - 18;
        text_draw_fit(tx + 18, ry + 14, 20, FONT_REGULAR, sel ? TH_TEXT : TH_TEXT_SOFT, ALIGN_LEFT,
                      stacked ? tw - 36 : tw * 0.58f, tr(r->name));
        if (sel && r->kind == K_ACTION)
            icon_draw(ICON_CHEVRON_RIGHT, tx + tw - 28, ry + 30, 22, TH_FOCUS);
        else if (r->kind == K_TOGGLE)
            draw_switch(tx + tw - 18 - 42, ry + 27, 19, read_value(r) ? 1.0f : 0.0f);
        else if (stacked)
            text_draw_fit(tx + 18, ry + 40, 19, FONT_REGULAR, sel ? TH_FOCUS : TH_TEXT_DIM,
                          ALIGN_LEFT, tw - 36, v);
        else
            text_draw_fit(tx + tw * 0.42f, ry + 28, 19, FONT_REGULAR, sel ? TH_FOCUS : TH_TEXT_DIM,
                          ALIGN_RIGHT, tw * 0.58f - 18, v);
        if (r->global_only && S.game_scope && app.game)
            icon_draw(ICON_WORLD, tx + 20, ry + 50, 18, TH_TEXT_DIM);
        if (r->kind == K_CHOICE)
            draw_meter(tx + 18, ry + th - 16, tw - 36, 4, row_fraction(r),
                       sel ? TH_FOCUS : argb_alpha(TH_FOCUS, 0.4f));
    }

    /* the phone page's code and address, under the phone row */
    if (phone_row >= 0)
    {
        float py = CONTENT_TOP + card_y - S.scroll;
        if (py < CONTENT_BOTTOM && py + phone_h > CONTENT_TOP)
        {
            draw_rrect(x, py, w, phone_h, TH_RADIUS, TH_CARD);
            draw_rrect_outline(x, py, w, phone_h, TH_RADIUS, 1, TH_DIVIDER);
            text_draw(x + 24, py + 14, 23, FONT_BOLD, TH_TEXT, ALIGN_LEFT,
                      tr("Settings from your phone"));
            const char *url = remote_address();
            if (app.global.remote && url[0])
            {
                draw_url_qr(url, x + 36, py + 58, 168);
                text_draw(x + 242, py + 58, 21, FONT_REGULAR, TH_TEXT_SOFT, ALIGN_LEFT,
                          tr("Scan with your phone"));
                /* the address, which is also what --host wants when signing in
                 * on a PC. The code itself is not printed: the QR carries it,
                 * and a 32-digit wall of hex is no use to anyone reading a TV. */
                text_draw_fit(x + 242, py + 88, 20, FONT_MONO, TH_TEXT, ALIGN_LEFT, w - 286,
                              remote_address_plain());
                text_draw(x + 242, py + 124, 16, FONT_REGULAR, TH_TEXT_SOFT, ALIGN_LEFT,
                          tr("Settings, saves and achievements sign-in"));
                text_draw(x + 242, py + 154, 15, FONT_REGULAR, TH_TEXT_DIM, ALIGN_LEFT,
                          tr("Only this QR opens the page."));
                text_draw(x + 242, py + 188, 15, FONT_MONO, TH_TEXT_DIM, ALIGN_LEFT,
                          "GBAR5 " PSXS5_VERSION);
            }
            else if (app.global.remote)
                text_draw(x + 24, py + 76, 21, FONT_REGULAR, TH_DANGER, ALIGN_LEFT,
                          tr("No network connection"));
            else
                text_draw(x + 24, py + 76, 21, FONT_REGULAR, TH_TEXT_DIM, ALIGN_LEFT,
                          tr("Turn it on to change settings from a phone."));
        }
    }
    plat_set_clip(0, 0, 0, 0);

    /* a soft fade where the board continues, and a scrollbar */
    if (y > view)
    {
        uint32_t fade[2] = {TH_BG_A(0x00), TH_BG_A(0xc0)};
        float stop[2] = {0.0f, 1.0f};
        draw_vgradient(x, CONTENT_BOTTOM - 46, w, 46, fade, stop, 2);
        uint32_t top[2] = {TH_BG_A(0xb0), TH_BG_A(0x00)};
        draw_vgradient(x, CONTENT_TOP - 8, w, 42, top, stop, 2);
        float knob = view * view / y;
        if (knob < 70)
            knob = 70;
        float travel = view - knob, at = S.scroll / (y - view);
        draw_rrect(x + w - 6, CONTENT_TOP, 4, view, 2, TH_PILL_A(0x70));
        draw_rrect(x + w - 6, CONTENT_TOP + travel * at, 4, knob, 2, argb_alpha(TH_FOCUS, 0.65f));
    }
}

/* ---------------------------------------------------------------- About: the hero */

/* The address under a QR card: 14px mono, dropped to 13px rather than clipped,
 * so the phone page's "http://192.168.1.20:8095/" keeps its tail. A "..." in a
 * URL is a bug, so if even 13px overruns we draw it whole. */
static void draw_qr_address(float cx, float y, const char *s)
{
    const float max_w = 200;
    float size = 14;
    if (text_width(size, FONT_MONO, s) > max_w)
        size = 13;
    if (text_width(size, FONT_MONO, s) <= max_w)
        text_draw_fit(cx, y, size, FONT_MONO, TH_TEXT_DIM, ALIGN_CENTER, max_w, s);
    else
        text_draw(cx, y, size, FONT_MONO, TH_TEXT_DIM, ALIGN_CENTER, s);
}

static void draw_about_tab(void)
{
    const float x = CONTENT_X, w = content_w(), hero_h = 300;
    bool hero_sel = row_focused(0);
    focus_card(x, CONTENT_TOP, w, hero_h, TH_RADIUS, hero_sel, true);
    text_draw(x + 40, CONTENT_TOP + 26, 86, FONT_BOLD, TH_TEXT, ALIGN_LEFT, "GBAR5");
    float name_w = text_width(86, FONT_BOLD, "GBAR5");
    draw_rrect(x + 40 + name_w + 24, CONTENT_TOP + 50, 150, 42, 21, TH_PILL);
    text_draw(x + 40 + name_w + 24 + 75, CONTENT_TOP + 60, 20, FONT_MONO, TH_FOCUS, ALIGN_CENTER,
              "v" PSXS5_VERSION);
    text_draw(x + 44, CONTENT_TOP + 132, 24, FONT_REGULAR, TH_TEXT_SOFT, ALIGN_LEFT,
              tr("Game Boy Advance emulation for the PS5."));
    text_draw(x + 44, CONTENT_TOP + 176, 18, FONT_MONO, TH_TEXT_DIM, ALIGN_LEFT,
              tr("This GBA emulator is made by me with love, for you to play all your retro games."));
    text_draw(x + 44, CONTENT_TOP + 236, 20, FONT_BOLD, TH_FOCUS, ALIGN_LEFT, tr("Built by Ronin"));
    /* the phone page's code while it is on; the project page otherwise,
     * and the tip jar next to it either way */
    const char *phone_url = remote_address();
    bool phone = app.global.remote && phone_url[0];
    const float gh_x = x + w - 400, kf_x = x + w - 214, qr_y = CONTENT_TOP + 50;
    float gh_side, kf_side;
    if (phone)
        gh_side = draw_url_qr(phone_url, gh_x, qr_y, 150);
    else
        gh_side = draw_asset_qr(&qr_project, "qr-github.png", gh_x, qr_y, 150);
    kf_side = draw_asset_qr(&qr_kofi, "qr-kofi.png", kf_x, qr_y, 150);
    /* each label sits on its own card's true centre: the drawn side is a whole
     * number of modules and is often smaller than the 150px box */
    const float gh_cx = gh_x + gh_side * 0.5f, kf_cx = kf_x + kf_side * 0.5f;
    text_draw(gh_cx, CONTENT_TOP + 16, 15, FONT_MONO, TH_TEXT_DIM, ALIGN_CENTER,
              tr(phone ? "Scan with your phone" : "GitHub"));
    text_draw(kf_cx, CONTENT_TOP + 16, 15, FONT_MONO, TH_TEXT_DIM, ALIGN_CENTER,
              tr("Support on Ko-fi"));
    /* the addresses without their codes, hard under each card: the QRs above
     * already carry them */
    draw_qr_address(gh_cx, qr_y + gh_side + 14 + 8,
                    phone ? remote_address_plain() : "github.com/thereal47Ronin/GBAR5");
    draw_qr_address(kf_cx, qr_y + kf_side + 14 + 8, "ko-fi.com/thereal47ronin");

    /* the readouts and the two buttons */
    const float gap = 14;
    float ty = CONTENT_TOP + hero_h + 18;
    struct
    {
        int row;
        float x, y, w, h;
    } tiles[4] = {
        {1, x, ty, w * 0.34f, 108},
        {2, x + w * 0.34f + gap, ty, w - w * 0.34f - gap, 108},
        {3, x, ty + 122, (w - gap) * 0.5f, 96},
        {4, x + (w - gap) * 0.5f + gap, ty + 122, (w - gap) * 0.5f, 96},
    };
    for (int i = 0; i < 4; ++i)
    {
        const Row *r = &ABOUT[tiles[i].row];
        note_row(tiles[i].row, tiles[i].x, tiles[i].y, tiles[i].w, tiles[i].h);
        bool sel = row_focused(tiles[i].row);
        focus_card(tiles[i].x, tiles[i].y, tiles[i].w, tiles[i].h, TH_RADIUS_SMALL, sel, true);
        text_draw_fit(tiles[i].x + 20, tiles[i].y + 16, 18, FONT_REGULAR,
                      sel ? TH_TEXT : TH_TEXT_DIM, ALIGN_LEFT, tiles[i].w - 40, tr(r->name));
        char buf[PSXS5_PATH_MAX + 64];
        const char *v = value_label(r, buf, sizeof(buf));
        if (r->kind == K_ACTION)
            v = update_state() == UPDATE_AVAILABLE ? tr("Install") : tr("Check now");
        text_draw_fit(tiles[i].x + 20, tiles[i].y + 44, 24, FONT_BOLD, sel ? TH_FOCUS : TH_TEXT,
                      ALIGN_LEFT, tiles[i].w - 40, v);
        if (r->kind == K_TOGGLE)
            draw_switch(tiles[i].x + tiles[i].w - 62, tiles[i].y + 40, 20, read_value(r) ? 1.0f : 0.0f);
        if (r->kind == K_ACTION)
            icon_draw(ICON_CHEVRON_RIGHT, tiles[i].x + tiles[i].w - 34, tiles[i].y + 46, 22,
                      sel ? TH_FOCUS : TH_TEXT_SOFT);
    }
    text_draw(x + 4, ty + 244, 16, FONT_MONO, argb_alpha(TH_TEXT_DIM, 0.75f), ALIGN_LEFT,
              tr("RetroAchievements  \xc2\xb7  all art drawn from scratch"));
}

static void activate(const Row *r)
{
    switch (r->special)
    {
    case SP_LOCATION:
        sfx_play(SFX_SELECT);
        app_next_data_location();
        return;
    case SP_NETWORK:
        sfx_play(SFX_SELECT);
        app_rescan_network();
        {
            char msg[64];
            snprintf(msg, sizeof(msg), tr("%d games on the shelf"), app.library.count);
            app_toast(msg);
        }
        return;
    case SP_REMAP:
        S.remap = true;
        S.remap_edit = false;
        S.remap_cursor = 0;
        S.remap_y = 0;
        sfx_play(SFX_SELECT);
        return;
    case SP_MEMCARDS:
        sfx_play(SFX_SELECT);
        memcards_open(SCREEN_SETTINGS);
        return;
    case SP_WHO:
        S.who = true;
        S.who_cursor = profiles_current();
        S.who_y = 0;
        sfx_play(SFX_SELECT);
        return;
    case SP_PROFILE:
        sfx_play(SFX_SELECT);
        profile_open(SCREEN_SETTINGS);
        return;
    case SP_PLAYERS:
        S.players = true;
        S.assigned = 0;
        for (int i = 0; i < PSXS5_MAX_PADS; ++i)
            S.pad_prev[i] = app.pads[i].buttons; /* the Cross that opened the page doesn't count */
        sfx_play(SFX_SELECT);
        return;
    case SP_STATS:
        sfx_play(SFX_SELECT);
        library_stats_open(SCREEN_SETTINGS);
        return;
    case SP_UPDATE:
        sfx_play(SFX_SELECT);
        if (update_state() == UPDATE_AVAILABLE)
            update_install();
        else
            update_check();
        return;
    case SP_RESCAN:
        if (app.game)
        {
            app_toast("Quit the game first");
            return;
        }
        if (app.storage_error[0])
            return;
        app_rescan();
        char msg[64];
        snprintf(msg, sizeof(msg), tr(app.library.count == 1 ? "Found %d game" : "Found %d games"),
                 app.library.count);
        app_toast(msg);
        sfx_play(SFX_SELECT);
        return;
    default:
        return;
    }
}

static void set_scope(bool game)
{
    if (!app.game || game == S.game_scope)
        return;
    if (game && !app.game_has_own)
    {
        /* this game's own settings start as a copy of the console-wide ones */
        app.game_has_own = true;
        S.drop_game = false;
        app_toast("This game now has its own settings");
    }
    S.game_scope = game;
    S.dirty = true;
    sfx_play(SFX_CLICK);
}

/* ---------------------------------------------------------------- the screen */

/* Save and Cancel, top right: the only two things that write anything. */
static float actions_w(void)
{
    return text_width(22, FONT_BOLD, tr("Save")) + text_width(22, FONT_BOLD, tr("Cancel")) + 130;
}

static void draw_actions(void)
{
    const char *save = tr("Save"), *cancel = tr("Cancel");
    const float ws = text_width(22, FONT_BOLD, save) + 44;
    const float wc = text_width(22, FONT_BOLD, cancel) + 44;
    const float right = plat_width() - TH_MARGIN, y = 46, h = 46;
    const float xc = right - wc, xs = xc - ws - 6;
    bool fs = S.action == 0, fc = S.action == 1;
    draw_rrect(xs - 6, y - 6, ws + wc + 18, h + 12, (h + 12) * 0.5f, TH_PILL_A(0xb0));
    draw_rrect(xs, y, ws, h, h * 0.5f, S.dirty ? TH_FOCUS : TH_PILL_A(0x90));
    text_draw(xs + ws * 0.5f, y + 11, 22, FONT_BOLD,
              S.dirty ? TH_BG : fs ? TH_TEXT : TH_TEXT_DIM, ALIGN_CENTER, save);
    draw_rrect_outline(xs, y, ws, h, h * 0.5f, fs ? 2 : 1, fs ? TH_TEXT : TH_DIVIDER);
    draw_rrect(xc, y, wc, h, h * 0.5f, fc ? TH_TEXT : TH_PILL_A(0x90));
    text_draw(xc + wc * 0.5f, y + 11, 22, FONT_BOLD, fc ? TH_BG : TH_TEXT_SOFT, ALIGN_CENTER,
              cancel);
    draw_rrect_outline(xc, y, wc, h, h * 0.5f, fc ? 2 : 1, fc ? TH_TEXT : TH_DIVIDER);
}

/* The "Save settings?" pop-up: asked once, when leaving with changes. */
static void draw_confirm(void)
{
    anim_approach(&S.confirm_t, 1.0f, app.dt, TH_SNAP);
    float t = S.confirm_t, e = 1.0f - (1.0f - t) * (1.0f - t);
    const float w = plat_width(), h = plat_height();
    draw_rect(0, 0, w, h, argb_alpha(0x00000000u, 0.66f * e));

    const float cw = 780, ch = 300;
    float cx = (w - cw) * 0.5f, cy = (h - ch) * 0.5f + (1.0f - e) * 26.0f;
    draw_rrect(cx, cy, cw, ch, TH_RADIUS, TH_BG_DEEP);
    draw_rrect_outline(cx, cy, cw, ch, TH_RADIUS, 1.5f, argb_alpha(TH_FOCUS, 0.55f * e));
    draw_glow(cx - 8, cy - 8, cw + 16, ch + 16, 26, argb_alpha(TH_FOCUS, 0.10f * e));

    icon_draw(ICON_DEVICE_FLOPPY, cx + 44, cy + 38, 34, TH_FOCUS);
    text_draw(cx + 92, cy + 38, 34, FONT_BOLD, TH_TEXT, ALIGN_LEFT, tr("Save settings?"));
    text_draw_fit(cx + 44, cy + 108, 21, FONT_REGULAR, TH_TEXT_SOFT, ALIGN_LEFT, cw - 88,
                  tr("You changed something. Save it before leaving?"));

    const char *yes = tr("Yes"), *no = tr("No");
    const float bh = 56, by = cy + ch - 86;
    const float wy = text_width(22, FONT_BOLD, yes) + 84, wn = text_width(22, FONT_BOLD, no) + 84;
    const float nx = cx + cw - 44 - wn, yx = nx - 12 - wy;
    bool on_yes = S.confirm_choice == 0;
    draw_rrect(yx, by, wy, bh, bh * 0.5f, on_yes ? TH_FOCUS : TH_PILL_A(0xa0));
    draw_rrect_outline(yx, by, wy, bh, bh * 0.5f, on_yes ? 2.0f : 1.0f,
                       on_yes ? TH_TEXT : TH_DIVIDER);
    text_draw(yx + wy * 0.5f, by + 15, 22, FONT_BOLD, on_yes ? TH_BG : TH_TEXT_SOFT, ALIGN_CENTER,
              yes);
    draw_rrect(nx, by, wn, bh, bh * 0.5f, on_yes ? TH_PILL_A(0xa0) : TH_TEXT);
    draw_rrect_outline(nx, by, wn, bh, bh * 0.5f, on_yes ? 1.0f : 2.0f,
                       on_yes ? TH_DIVIDER : TH_TEXT);
    text_draw(nx + wn * 0.5f, by + 15, 22, FONT_BOLD, on_yes ? TH_TEXT_SOFT : TH_BG, ALIGN_CENTER,
              no);
}

static void draw_header(void)
{
    const char *title = S.remap ? "Button mapping" : S.players ? "Player order"
                        : S.who   ? "Who's playing"
                                  : "Settings";
    text_draw(TH_MARGIN, 40, 44, FONT_BOLD, TH_TEXT, ALIGN_LEFT, tr("Settings"));
    draw_actions();
    if (S.remap || S.players || S.who)
    {
        /* the sub-pages get their own name on the right of the wordmark */
        text_draw(TH_MARGIN + text_width(44, FONT_BOLD, tr("Settings")) + 22, 52, 24,
                  FONT_REGULAR, TH_FOCUS, ALIGN_LEFT, tr(title));
        return;
    }
    char sub[200];
    if (app.game)
        snprintf(sub, sizeof(sub), "%s  \xc2\xb7  %s", app.game->title,
                 tr(S.game_scope ? "This game" : "All games"));
    else
        str_copy(sub, sizeof(sub), tr("Applies to every game"));
    text_draw_fit(TH_MARGIN + 2, 100, 22, FONT_REGULAR, TH_TEXT_DIM, ALIGN_LEFT, 1000, sub);

    if (!app.game)
        return;
    /* scope switch, left of the Save / Cancel buttons */
    const char *a = tr("This game"), *b = tr("All games");
    float wa = text_width(22, FONT_REGULAR, a) + 40, wb = text_width(22, FONT_REGULAR, b) + 40;
    float right = plat_width() - TH_MARGIN - actions_w() - 26,
          hintw = pad_glyph_width(GLYPH_L2, 26) * 2 + 30;
    float xb = right - hintw - wb, xa = xb - wa - 6, y = 46;
    float target = S.game_scope ? xa : xb;
    if (S.scope_x == 0)
        S.scope_x = target;
    anim_approach(&S.scope_x, target, app.dt, TH_SNAP);
    draw_rrect(xa - 6, y - 6, wa + wb + 18, 58, 29, TH_PILL_A(0xc0));
    draw_rrect(S.scope_x, y, S.game_scope ? wa : wb, 46, 23, TH_TEXT);
    text_draw(xa + 20, y + 11, 22, FONT_REGULAR, S.game_scope ? TH_BG : TH_HINT, ALIGN_LEFT, a);
    text_draw(xb + 20, y + 11, 22, FONT_REGULAR, S.game_scope ? TH_HINT : TH_BG, ALIGN_LEFT, b);
    float gx = right - hintw + 16;
    gx += draw_pad_glyph(GLYPH_L2, gx + pad_glyph_width(GLYPH_L2, 26) * 0.5f, y + 23, 26) + 6;
    draw_pad_glyph(GLYPH_R2, gx + pad_glyph_width(GLYPH_R2, 26) * 0.5f, y + 23, 26);
}

void settings_screen(uint32_t pressed)
{
    if (app.game)
        app_draw_game(45);
    else
        shelf_backdrop();
    if (app.game)
        draw_rect(0, 0, plat_width(), plat_height(), TH_SCRIM);
    draw_header();

    if (S.remap)
    {
        remap_page(pressed);
        app_draw_toast();
        return;
    }
    if (S.players)
    {
        players_page(pressed);
        app_draw_toast();
        return;
    }
    if (S.who)
    {
        who_page(pressed);
        app_draw_toast();
        return;
    }

    draw_rail();
    const Tab *t = tab();
    if (S.cursor < 0 || S.cursor >= t->count)
        first_selectable();

    /* ------------------------------------------------ input */
    /* One rule for the whole screen: the D-pad (or the left stick) moves the
     * focus the way it looks, Cross goes in / starts an edit, Circle backs out
     * one step, and while a setting is being changed left / right picks its
     * value, Cross keeps it and Circle puts it back. */
    /* the Save? pop-up takes every button while it is up */
    bool confirm_up = S.confirm;
    if (S.confirm)
    {
        if (pressed & (BIT(BTN_LEFT) | BIT(BTN_RIGHT) | BIT(BTN_UP) | BIT(BTN_DOWN)))
        {
            S.confirm_choice ^= 1;
            sfx_play(SFX_CLICK);
        }
        if (pressed & BIT(BTN_CROSS))
        {
            confirm_close(S.confirm_choice == 0);
            return;
        }
        if (pressed & BIT(BTN_CIRCLE))
        {
            S.confirm = false;
            sfx_play(SFX_BACK);
        }
    }

    if (!confirm_up && !S.editing)
    {
        if (pressed & BIT(BTN_L2))
            set_scope(true);
        if (pressed & BIT(BTN_R2))
            set_scope(false);
    }

    if (!confirm_up && S.rail)
    {
        /* the tabs: up / down picks one, Cross opens it, and Up from the first
         * tab reaches the Save / Cancel buttons */
        int step = (pressed & BIT(BTN_DOWN)) ? 1 : (pressed & BIT(BTN_UP)) ? -1 : 0;
        if (step < 0 && S.tab == 0)
        {
            S.action = 0;
            sfx_play(SFX_CLICK);
            step = 0;
        }
        if (step)
        {
            S.tab = (S.tab + step + TAB_COUNT) % TAB_COUNT;
            first_selectable();
            S.scroll = 0;
            S.sel_y = -1;
            S.row_rects = 0;
            sfx_play(SFX_CLICK);
        }
        if (pressed & BIT(BTN_CROSS))
        {
            S.rail = false;
            S.editing = false;
            sfx_play(SFX_SELECT);
        }
        if (pressed & BIT(BTN_CIRCLE))
        {
            sfx_play(SFX_BACK);
            settings_leave();
            return;
        }
    }
    else if (S.action >= 0)
    {
        /* the Save / Cancel buttons */
        if (pressed & (BIT(BTN_LEFT) | BIT(BTN_RIGHT)))
        {
            S.action = S.action ? 0 : 1;
            sfx_play(SFX_CLICK);
        }
        if (pressed & BIT(BTN_CROSS))
        {
            if (S.action == 0)
                settings_commit();
            else
            {
                sfx_play(SFX_BACK);
                settings_discard();
                app_toast(tr("Changes discarded"));
                app.screen = app.settings_return;
                return;
            }
        }
        if (pressed & (BIT(BTN_CIRCLE) | BIT(BTN_UP) | BIT(BTN_DOWN)))
        {
            S.action = -1;
            sfx_play(SFX_BACK);
        }
    }
    else if (S.editing)
    {
        const Row *e = S.cursor >= 0 ? &t->rows[S.cursor] : NULL;
        if (e && e->kind == K_COLOR)
        {
            /* a hex code: Left / Right picks one of the six digits, Up / Down
             * turns it, and the change is live on this frame */
            int side = (pressed & BIT(BTN_RIGHT)) ? 1 : (pressed & BIT(BTN_LEFT)) ? -1 : 0;
            int turn = (pressed & BIT(BTN_DOWN)) ? 1 : (pressed & BIT(BTN_UP)) ? -1 : 0;
            if (side)
            {
                S.hex_pos = (S.hex_pos + side + 6) % 6;
                sfx_play(SFX_CLICK);
            }
            if (turn)
            {
                hex_step(e, turn);
                sfx_play(SFX_CLICK);
            }
            if (pressed & BIT(BTN_CROSS))
                end_edit(e, true);
            else if (pressed & BIT(BTN_CIRCLE))
                end_edit(e, false);
        }
        else
        {
            int delta = (pressed & BIT(BTN_RIGHT)) ? 1 : (pressed & BIT(BTN_LEFT)) ? -1 : 0;
            if (e && delta)
            {
                step_value(e, delta);
                sfx_play(SFX_CLICK); /* also previews a new interface sound */
            }
            if (pressed & BIT(BTN_CROSS))
                end_edit(e, true);
            else if (pressed & BIT(BTN_CIRCLE))
                end_edit(e, false);
        }
    }
    else
    {
        /* the options: every direction moves to the row that way */
        int dx = (pressed & BIT(BTN_RIGHT)) ? 1 : (pressed & BIT(BTN_LEFT)) ? -1 : 0;
        int dy = (pressed & BIT(BTN_DOWN)) ? 1 : (pressed & BIT(BTN_UP)) ? -1 : 0;
        if ((dx || dy) && !move_focus(dx, dy) && dy < 0)
        {
            /* nothing above: the Save / Cancel buttons live up there */
            S.action = 0;
            sfx_play(SFX_CLICK);
        }
        const Row *f = S.cursor >= 0 ? &t->rows[S.cursor] : NULL;
        if (f)
        {
            if (pressed & BIT(BTN_CROSS))
            {
                if (f->kind == K_TOGGLE)
                {
                    /* a switch flips on Cross: one press turns it on */
                    write_value(f, read_value(f) ? 0 : 1);
                    sfx_play(SFX_CLICK);
                }
                else if (f->kind == K_CHOICE || f->kind == K_COLOR)
                    begin_edit(f);
                else if (f->kind == K_ACTION)
                    activate(f);
            }
            if (pressed & BIT(BTN_TRIANGLE))
            {
                default_value(f);
                sfx_play(SFX_CLICK);
            }
        }
        if (pressed & BIT(BTN_CIRCLE))
        {
            S.rail = true;
            sfx_play(SFX_BACK);
        }
        if ((pressed & BIT(BTN_SQUARE)) && app.game && app.game_has_own)
        {
            /* back to the console-wide settings for this game; the file goes on Save */
            app.game_has_own = false;
            app.settings = app.global;
            host_apply_settings(&app.settings);
            S.game_scope = false;
            S.drop_game = true;
            S.dirty = true;
            app_toast("This game uses the settings for all games again");
        }
    }

    const Row *r = S.cursor >= 0 ? &t->rows[S.cursor] : NULL;

    /* ------------------------------------------------ the tab's own layout */
    S.row_rects = 0;
    if (t->rows == DISPLAY)
        draw_screen_tab();
    else if (t->rows == CONTROLS)
        draw_controls_tab();
    else if (t->rows == SOUND)
        draw_sound_tab();
    else if (t->rows == LIBRARY)
        draw_shelf_tab();
    else if (t->rows == SYSTEM)
        draw_system_tab();
    else
        draw_about_tab();
    draw_edit_frame();
    if (S.rail)
        draw_caption(tr(t->name), NULL, false);
    else
        draw_caption(r ? tr(r->name) : NULL, r ? tr(r->help) : NULL, S.editing);
    if (S.editing && r && r->kind == K_COLOR)
    {
        static const int glyphs[] = {GLYPH_UP, GLYPH_LEFT, GLYPH_CROSS, GLYPH_CIRCLE};
        static const char *const labels[] = {"Turn digit", "Pick digit", "Keep", "Undo"};
        app_draw_hints(glyphs, labels, 4,
                       "Up / Down turns the digit; Left / Right picks the next one");
    }
    else if (S.editing)
    {
        static const int glyphs[] = {GLYPH_LEFT, GLYPH_CROSS, GLYPH_CIRCLE};
        static const char *const labels[] = {"Change", "Keep", "Undo"};
        app_draw_hints(glyphs, labels, 3, "Left / Right changes it");
    }
    else if (S.action >= 0)
    {
        static const int glyphs[] = {GLYPH_CROSS, GLYPH_CIRCLE};
        static const char *const labels[] = {"Select", "Back"};
        app_draw_hints(glyphs, labels, 2, "Save writes the changes; Cancel puts them back");
    }
    else if (S.rail)
    {
        static const int glyphs[] = {GLYPH_CROSS, GLYPH_CIRCLE};
        static const char *const labels[] = {"Open", "Back"};
        app_draw_hints(glyphs, labels, 2, "Up / Down  Tabs");
    }
    else if (app.game && app.game_has_own && S.game_scope)
    {
        static const int glyphs[] = {GLYPH_CROSS, GLYPH_CIRCLE, GLYPH_TRIANGLE, GLYPH_SQUARE};
        static const char *const labels[] = {"Select", "Back", "Default", "Use all-games settings"};
        app_draw_hints(glyphs, labels, 4, "Directions  Move");
    }
    else
    {
        static const int glyphs[] = {GLYPH_CROSS, GLYPH_CIRCLE, GLYPH_TRIANGLE};
        static const char *const labels[] = {"Select", "Back", "Default"};
        app_draw_hints(glyphs, labels, 3, "Directions  Move");
    }
    app_draw_toast();
    if (S.confirm)
        draw_confirm();
    else
        S.confirm_t = 0;
}

/* ---------------------------------------------------------------- the phone page */

static size_t json_text(char *out, size_t size, size_t at, const char *s)
{
    if (at + 2 >= size)
        return at;
    out[at++] = '"';
    for (; *s && at + 8 < size; ++s)
    {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\')
        {
            out[at++] = '\\';
            out[at++] = (char)c;
        }
        else if (c < 0x20)
            at += (size_t)snprintf(out + at, size - at, "\\u%04x", c);
        else
            out[at++] = (char)c;
    }
    out[at++] = '"';
    out[at] = '\0';
    return at;
}

#define PUT(...) (at += (size_t)snprintf(out + at, at < size ? size - at : 0, __VA_ARGS__))

/* Every tab and setting the phone page shows, with values, in the menus' language. */
int settings_json(char *out, size_t size)
{
    size_t at = 0;
    out[0] = '\0';
    PUT("{\"game\":");
    at = json_text(out, size, at, app.game ? app.game->title : tr("Applies to every game"));
    PUT(",\"tabs\":[");
    for (int t = 0; t < TAB_COUNT && at < size; ++t)
    {
        PUT("%s{\"name\":", t ? "," : "");
        at = json_text(out, size, at, tr(TABS[t].name));
        PUT(",\"rows\":[");
        bool first = true;
        for (int i = 0; i < TABS[t].count && at < size; ++i)
        {
            const Row *r = &TABS[t].rows[i];
            if (r->kind == K_ACTION || r->kind == K_COLOR)
                continue; /* menus, pages and #hex colours stay on the console */
            PUT("%s{\"key\":\"%d.%d\",\"name\":", first ? "" : ",", t, i);
            first = false;
            at = json_text(out, size, at, tr(r->name));
            if (r->group)
            {
                PUT(",\"group\":");
                at = json_text(out, size, at, tr(r->group));
            }
            PUT(",\"help\":");
            at = json_text(out, size, at, tr(r->help));
            if (r->kind == K_INFO)
            {
                char buf[PSXS5_PATH_MAX + 64];
                PUT(",\"kind\":\"info\",\"text\":");
                at = json_text(out, size, at, value_label(r, buf, sizeof(buf)));
            }
            else
            {
                PUT(",\"kind\":\"%s\",\"value\":%d,\"labels\":[", r->kind == K_TOGGLE ? "toggle" : "choice",
                    read_value(r));
                int count = r->kind == K_TOGGLE ? 2 : r->count;
                for (int v = 0; v < count && at < size; ++v)
                {
                    PUT("%s", v ? "," : "");
                    at = json_text(out, size, at, r->special == SP_LANGUAGE ? i18n_name(v) : tr(r->labels[v]));
                }
                PUT("]");
            }
            PUT("}");
        }
        PUT("]}");
    }
    /* the RetroAchievements account, for the sign-in card on the Achievements tab */
    int ra_tab = 0;
    for (int t = 0; t < TAB_COUNT; ++t)
        if (TABS[t].rows == ACHIEVEMENTS)
            ra_tab = t;
    PUT("],\"ra\":{\"tab\":%d,\"signed_in\":%s,\"user\":", ra_tab, ra_signed_in() ? "true" : "false");
    at = json_text(out, size, at, ra_user());
    PUT(",\"profile\":");
    at = json_text(out, size, at, profiles_name(profiles_current()));
    /* the network shares, for the card on the System tab (never their passwords) */
    int net_tab = 0;
    for (int t = 0; t < TAB_COUNT; ++t)
        if (TABS[t].rows == SYSTEM)
            net_tab = t;
    PUT("},\"net\":{\"tab\":%d,\"shares\":[", net_tab);
    for (int i = 0; i < netfs_share_count() && at < size; ++i)
    {
        PUT("%s{\"url\":", i ? "," : "");
        at = json_text(out, size, at, netfs_share_url(i));
        PUT(",\"status\":");
        at = json_text(out, size, at, netfs_share_status(i));
        PUT("}");
    }
    PUT("]},\"colour\":{\"tab\":");
    int shelf_tab = 0;
    for (int t = 0; t < TAB_COUNT; ++t)
        if (TABS[t].rows == LIBRARY)
            shelf_tab = t;
    PUT("%d,\"bg\":\"%06x\",\"accent\":\"%06x\",\"custom\":%s}}", shelf_tab,
        (unsigned)(theme_custom_bg() & 0xffffffu), (unsigned)(theme_custom_accent() & 0xffffffu),
        read_value(&LIBRARY[0]) == THEME_CUSTOM ? "true" : "false");
    return (int)(at < size ? at : size - 1);
}

/* A change from the phone: "tab.row" and the new value. Returns the setting's
 * name, or NULL when the key is unknown. */
const char *settings_set_by_key(const char *key, int value)
{
    int t = -1, i = -1;
    if (sscanf(key, "%d.%d", &t, &i) != 2 || t < 0 || t >= TAB_COUNT || i < 0 || i >= TABS[t].count)
        return NULL;
    const Row *r = &TABS[t].rows[i];
    if (r->kind != K_CHOICE && r->kind != K_TOGGLE)
        return NULL;
    int count = r->kind == K_TOGGLE ? 2 : r->count;
    if (value < 0 || value >= count)
        return NULL;
    /* the phone edits what the console's settings would: this game's own when it has them */
    bool scope = S.game_scope;
    S.game_scope = app.game && app.game_has_own;
    write_value(r, value);
    S.game_scope = scope;
    return r->name;
}

/* A Custom colour typed on the phone page: "accent" or "bg", six hex digits.
 * Switches to the Custom theme, so the console changes under the finger. */
bool settings_set_colour(const char *key, const char *hex)
{
    uint32_t c;
    if (!theme_parse_hex(hex, &c))
        return false;
    if (strcmp(key, "accent") == 0)
        app.global.custom_accent = c;
    else if (strcmp(key, "bg") == 0)
        app.global.custom_bg = c;
    else
        return false;
    app.global.theme = THEME_CUSTOM;
    theme_customize(app.global.custom_bg, app.global.custom_accent);
    theme_apply(app.global.theme);
    return true;
}
