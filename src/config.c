/*
 * PSXS5 - settings stored as key=value lines in <root>/psxs5.ini.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "config.h"

#include "i18n.h"
#include "ui/theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void config_defaults(Settings *s)
{
    memset(s, 0, sizeof(*s));
    s->aspect = ASPECT_AUTO;
    s->smooth = true;
    s->upscale = 2;
    s->upscale_filter = UPSCALE_SHARP; /* GBA: crisp pixels by default */
    s->cover_style = COVER_FLAT;
    s->cover_download = true;
    s->shelf_card = SHELF_CARD_SNAP; /* the box shows the game, not its cover */
    s->ui_sound = 0;  /* Soft */
    s->ui_volume = 1; /* 50 % */
    s->rumble = true;
    s->quick_resume = true;
    s->update_check = true;
    s->remote = false; /* settings from a phone: off until you ask for it (and its page needs this console's code) */
    s->rumble_strength = 3;
    s->ra_popups = true;
    s->ra_tracker = true;
    s->trigger_effects = true;
    s->brightness = 1;
    s->game_fixes = true;
    s->negcon = true;
    s->touch_mouse = true;
    s->racing_triggers = true;
    /* GBAR5 defaults: what a GBA looked and sounded like, without the extras
     * turned up. Colour correction on (the GBA's own palette), no ghosting,
     * the real BIOS when one is in bios/, skip its boot logo. */
    s->colour_correction = 1;
    s->ghosting = 0;
    s->frameskip = 0;
    s->use_bios = false;
    s->skip_bios = true;
    s->idle_opt = 0;
    s->solar = 0;
    s->gbp_rumble = false;
    s->opposing = false;
    s->low_pass = 0;
    /* the Custom theme starts on Emerald's own two colours, so switching to
     * it changes nothing until someone edits them */
    s->custom_bg = 0x07110c;
    s->custom_accent = 0x3fd08a;
    for (int i = 0; i < 16; ++i)
        s->button_map[i] = (int8_t)i;
}

static bool as_bool(const char *v)
{
    return strcmp(v, "1") == 0 || strcmp(v, "true") == 0 || strcmp(v, "on") == 0;
}

/* Applies the keys in a file over what *s already holds. */
static bool config_apply(Settings *s, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    char line[256];
    while (fgets(line, sizeof(line), f))
    {
        char *eq = strchr(line, '=');
        if (!eq || line[0] == '#')
            continue;
        *eq = '\0';
        char *key = line, *value = eq + 1;
        value[strcspn(value, "\r\n")] = '\0';
        if (strcmp(key, "aspect") == 0)
            s->aspect = atoi(value) % ASPECT_COUNT;
        else if (strcmp(key, "smooth") == 0)
            s->smooth = as_bool(value);
        else if (strcmp(key, "show_fps") == 0)
            s->show_fps = as_bool(value);
        else if (strcmp(key, "force_hle") == 0)
            s->force_hle = as_bool(value);
        else if (strcmp(key, "state_slot") == 0)
            s->state_slot = atoi(value) % 10;
        else if (strcmp(key, "last_game") == 0)
            s->last_game = atoi(value);
        else if (strcmp(key, "stick_dpad") == 0)
            s->stick_dpad = atoi(value) % STICK_DPAD_COUNT;
        else if (strcmp(key, "integer_scale") == 0)
            s->integer_scale = as_bool(value);
        else if (strcmp(key, "upscale") == 0)
            s->upscale = atoi(value) >= 1 && atoi(value) <= 4 ? atoi(value) : 1;
        else if (strcmp(key, "upscale_filter") == 0)
            s->upscale_filter = atoi(value) % UPSCALE_FILTER_COUNT;
        else if (strcmp(key, "cover_style") == 0)
            s->cover_style = atoi(value) % COVER_STYLE_COUNT;
        else if (strcmp(key, "cover_download") == 0)
            s->cover_download = as_bool(value);
        else if (strcmp(key, "shelf_card") == 0)
            s->shelf_card = atoi(value) % SHELF_CARD_COUNT;
        else if (strcmp(key, "ui_sounds") == 0 && !as_bool(value))
            s->ui_sound = 5; /* older config: sounds off -> SFX_STYLE_OFF */
        else if (strcmp(key, "ui_sound") == 0)
            s->ui_sound = atoi(value) % 6;
        else if (strcmp(key, "language") == 0)
            s->language = atoi(value) % LANG_COUNT;
        else if (strcmp(key, "ui_volume") == 0)
            s->ui_volume = atoi(value) % 4;
        else if (strcmp(key, "rumble") == 0)
            s->rumble = as_bool(value);
        else if (strcmp(key, "rumble_strength") == 0)
            s->rumble_strength = atoi(value) % 4;
        else if (strcmp(key, "sort_mode") == 0)
            s->sort_mode = atoi(value) % 8;
        else if (strcmp(key, "widescreen") == 0)
            s->widescreen = as_bool(value);
        else if (strcmp(key, "multitap") == 0)
            s->multitap = as_bool(value);
        else if (strcmp(key, "theme") == 0)
        {
            int t = atoi(value);
            if (t < 0)
                t = 0;
            s->theme = t % THEME_COUNT;
        }
        else if (strcmp(key, "custom_bg") == 0)
        {
            uint32_t c;
            if (theme_parse_hex(value, &c))
                s->custom_bg = c;
        }
        else if (strcmp(key, "custom_accent") == 0)
        {
            uint32_t c;
            if (theme_parse_hex(value, &c))
                s->custom_accent = c;
        }
        else if (strcmp(key, "shader") == 0)
            s->shader = atoi(value) % 3;
        else if (strcmp(key, "crop_edges") == 0)
            s->crop_edges = atoi(value) % 3;
        else if (strcmp(key, "ra_popups") == 0)
            s->ra_popups = as_bool(value);
        else if (strcmp(key, "ra_tracker") == 0)
            s->ra_tracker = as_bool(value);
        else if (strcmp(key, "ra_popup_style") == 0)
            s->ra_popup_style = atoi(value) % 3;
        else if (strcmp(key, "lightbar") == 0)
            s->lightbar = atoi(value) % 3;
        else if (strcmp(key, "trigger_effects") == 0)
            s->trigger_effects = as_bool(value);
        else if (strcmp(key, "racing_triggers") == 0)
            s->racing_triggers = as_bool(value);
        else if (strcmp(key, "rumble_feel") == 0)
            s->rumble_feel = atoi(value) % 4;
        else if (strcmp(key, "lightgun") == 0)
            s->lightgun = atoi(value) % 3;
        else if (strcmp(key, "brightness") == 0)
            s->brightness = atoi(value) % 4;
        else if (strcmp(key, "colour") == 0)
            s->colour = atoi(value) % 6;
        else if (strcmp(key, "autosave") == 0)
            s->autosave = atoi(value) % 4;
        else if (strcmp(key, "bezel") == 0)
            s->bezel = as_bool(value);
        else if (strcmp(key, "game_fixes") == 0)
            s->game_fixes = as_bool(value);
        else if (strcmp(key, "sharpen") == 0)
            s->sharpen = atoi(value) % 3;
        else if (strcmp(key, "negcon") == 0)
            s->negcon = as_bool(value);
        else if (strcmp(key, "touch_mouse") == 0)
            s->touch_mouse = as_bool(value);
        else if (strcmp(key, "run_ahead") == 0)
            s->run_ahead = atoi(value) % 3;
        else if (strcmp(key, "framegen") == 0)
        s->framegen = as_bool(value);
    else if (strcmp(key, "colour_correction") == 0)
        s->colour_correction = atoi(value) % 3;
    else if (strcmp(key, "ghosting") == 0)
        s->ghosting = atoi(value) % 4;
    else if (strcmp(key, "frameskip") == 0)
        s->frameskip = as_bool(value);
    else if (strcmp(key, "use_bios") == 0)
        s->use_bios = as_bool(value);
    else if (strcmp(key, "skip_bios") == 0)
        s->skip_bios = as_bool(value);
    else if (strcmp(key, "idle_opt") == 0)
        s->idle_opt = atoi(value) % 3;
    else if (strcmp(key, "solar") == 0)
        s->solar = atoi(value) % 4;
    else if (strcmp(key, "gbp_rumble") == 0)
        s->gbp_rumble = as_bool(value);
    else if (strcmp(key, "opposing") == 0)
        s->opposing = as_bool(value);
    else if (strcmp(key, "low_pass") == 0)
        s->low_pass = atoi(value) % 4;
        else if (strcmp(key, "rewind") == 0)
            s->rewind = as_bool(value);
        else if (strcmp(key, "quick_resume") == 0)
            s->quick_resume = as_bool(value);
        else if (strcmp(key, "crt") == 0)
            s->crt = atoi(value) % 3;
        else if (strcmp(key, "border") == 0)
            s->border = atoi(value) % 3;
        else if (strcmp(key, "remote") == 0)
            s->remote = as_bool(value);
        else if (strcmp(key, "update_check") == 0)
            s->update_check = as_bool(value);
        else if (strcmp(key, "background") == 0)
            s->background = atoi(value) % 2;
        else if (strcmp(key, "shelf_category") == 0)
            s->shelf_category = atoi(value) % 16;
        else if (strcmp(key, "button_map") == 0)
        {
            char *p = value;
            for (int i = 0; i < 16 && *p; ++i)
            {
                int v = (int)strtol(p, &p, 10);
                s->button_map[i] = (int8_t)(v >= -1 && v < 16 ? v : i);
                if (*p == ',')
                    ++p;
            }
        }
    }
    fclose(f);
    return true;
}

/* A file beside `path` with another name, so an install made before the
 * GBAR5 rename keeps its settings. */
static void sibling_path(const char *path, const char *name, char *out, size_t size)
{
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    if (backslash > slash)
        slash = backslash;
    if (!slash)
    {
        snprintf(out, size, "%s", name);
        return;
    }
    snprintf(out, size, "%.*s%s", (int)(slash - path + 1), path, name);
}

static int config_peek_int(const char *path, const char *key, int fallback);

void config_load(Settings *s, const char *path)
{
    config_defaults(s);
    if (!config_apply(s, path))
    {
        /* GBAR5 reads the settings GBAS5 wrote, until the next save replaces them */
        char legacy[PSXS5_PATH_MAX];
        sibling_path(path, "gbas5.ini", legacy, sizeof(legacy));
        if (strcmp(legacy, path) != 0 && path_exists(legacy))
            config_apply(s, legacy);
    }
    if (!config_peek_int(path, "lang_rev", 0))
    {
        /* before the nine languages, the order was EN, FR, PT, ES, JA */
        if (s->language == LANG_OLD_PT)
            s->language = LANG_PT;
        else if (s->language == LANG_OLD_ES)
            s->language = LANG_ES;
        else if (s->language == LANG_OLD_JA)
            s->language = LANG_JA;
    }
}

bool config_load_game(Settings *out, const Settings *global, const char *path)
{
    *out = *global;
    if (!config_apply(out, path))
        return false;
    /* these always follow the console-wide settings */
    out->last_game = global->last_game;
    out->cover_style = global->cover_style;
    out->cover_download = global->cover_download;
    out->shelf_card = global->shelf_card;
    out->ui_sound = global->ui_sound;
    out->ui_volume = global->ui_volume;
    out->language = global->language;
    out->sort_mode = global->sort_mode;
    out->shelf_category = global->shelf_category;
    out->background = global->background;
    out->remote = global->remote;
    out->update_check = global->update_check;
    out->quick_resume = global->quick_resume;
    return true;
}

/* One setting read straight from a settings file, before the rest is loaded
 * (frame generation's 120 Hz mode is chosen when the screen opens) */
bool config_peek_bool(const char *path, const char *key)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    char line[256];
    size_t n = strlen(key);
    bool on = false;
    while (fgets(line, sizeof(line), f))
        if (!strncmp(line, key, n) && line[n] == '=')
            on = atoi(line + n + 1) != 0;
    fclose(f);
    return on;
}

/* One integer, or `fallback` when the file or the key isn't there. */
static int config_peek_int(const char *path, const char *key, int fallback)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return fallback;
    char line[256];
    size_t n = strlen(key);
    int value = fallback;
    while (fgets(line, sizeof(line), f))
        if (!strncmp(line, key, n) && line[n] == '=')
            value = atoi(line + n + 1);
    fclose(f);
    return value;
}

bool config_save(const Settings *s, const char *path)
{
    char temp[PSXS5_PATH_MAX];
    snprintf(temp, sizeof(temp), "%s.tmp", path);
    FILE *f = fopen(temp, "w");
    if (!f)
        return false;
    fprintf(f,
            "# Game Boy Advance settings\n"
            "lang_rev=1\n"
            "ui_rev=1\n"
            "aspect=%d\nsmooth=%d\nshow_fps=%d\nforce_hle=%d\n"
            "state_slot=%d\nlast_game=%d\n"
            "cover_style=%d\ncover_download=%d\nui_sound=%d\nui_volume=%d\n"
            "integer_scale=%d\nupscale=%d\nupscale_filter=%d\nstick_dpad=%d\nlanguage=%d\n"
            "rumble=%d\nrumble_strength=%d\nsort_mode=%d\nshelf_category=%d\nbackground=%d\n",
            s->aspect, s->smooth, s->show_fps, s->force_hle, s->state_slot, s->last_game,
            s->cover_style, s->cover_download, s->ui_sound, s->ui_volume, s->integer_scale,
            s->upscale, s->upscale_filter, s->stick_dpad, s->language, s->rumble,
            s->rumble_strength, s->sort_mode, s->shelf_category, s->background);
    fprintf(f, "widescreen=%d\nmultitap=%d\nrewind=%d\nquick_resume=%d\ncrt=%d\nborder=%d\nremote=%d\nupdate_check=%d\n",
            s->widescreen, s->multitap, s->rewind, s->quick_resume, s->crt, s->border, s->remote,
            s->update_check);
    fprintf(f, "ra_popups=%d\nra_tracker=%d\ncrop_edges=%d\n", s->ra_popups, s->ra_tracker, s->crop_edges);
    fprintf(f, "ra_popup_style=%d\nlightbar=%d\nshader=%d\ntheme=%d\n", s->ra_popup_style,
            s->lightbar, s->shader, s->theme);
    fprintf(f, "custom_bg=%06x\ncustom_accent=%06x\n", (unsigned)(s->custom_bg & 0xffffffu),
            (unsigned)(s->custom_accent & 0xffffffu));
    fprintf(f, "trigger_effects=%d\nracing_triggers=%d\nrumble_feel=%d\n",
            s->trigger_effects, s->racing_triggers, s->rumble_feel);
    fprintf(f, "lightgun=%d\nbrightness=%d\ncolour=%d\nautosave=%d\nbezel=%d\n", s->lightgun,
            s->brightness, s->colour, s->autosave, s->bezel);
    fprintf(f, "colour_correction=%d\nghosting=%d\nframeskip=%d\nuse_bios=%d\nskip_bios=%d\n"
               "idle_opt=%d\nsolar=%d\ngbp_rumble=%d\nopposing=%d\nlow_pass=%d\n",
            s->colour_correction, s->ghosting, s->frameskip, s->use_bios, s->skip_bios,
            s->idle_opt, s->solar, s->gbp_rumble, s->opposing, s->low_pass);
    fprintf(f, "game_fixes=%d\nsharpen=%d\nnegcon=%d\n", s->game_fixes, s->sharpen, s->negcon);
    fprintf(f, "touch_mouse=%d\nrun_ahead=%d\nframegen=%d\nshelf_card=%d\n", s->touch_mouse,
            s->run_ahead, s->framegen, s->shelf_card);
    fprintf(f, "button_map=");
    for (int i = 0; i < 16; ++i)
        fprintf(f, i ? ",%d" : "%d", s->button_map[i]);
    fprintf(f, "\n");
    bool ok = fclose(f) == 0;
    return ok && rename(temp, path) == 0;
}

void config_paths(Paths *p, const char *root)
{
    str_copy(p->root, sizeof(p->root), root);
    path_join(p->games, sizeof(p->games), root, "games");
    path_join(p->bios, sizeof(p->bios), root, "bios");
    path_join(p->saves, sizeof(p->saves), root, "saves");
    path_join(p->states, sizeof(p->states), root, "states");
    path_join(p->cheats, sizeof(p->cheats), root, "cheats");
    path_join(p->covers, sizeof(p->covers), root, "covers");
    path_join(p->logs, sizeof(p->logs), root, "logs");
    path_join(p->config, sizeof(p->config), root, "gbar5.ini");
    str_copy(p->user, sizeof(p->user), root);
}

void config_user_paths(Paths *p, const char *dir)
{
    str_copy(p->user, sizeof(p->user), dir);
    path_join(p->saves, sizeof(p->saves), dir, "saves");
    path_join(p->states, sizeof(p->states), dir, "states");
    path_join(p->config, sizeof(p->config), dir, "gbar5.ini");
}
