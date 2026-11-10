/*
 * GBAR5 - the themes: Emerald (the default), Pocket (light), Kiwi, Onyx,
 * Jade, Sapphire, Amethyst, Ruby, Amber, Rose, and Custom - any background
 * and accent colour at all, from a hex code, with the rest of the palette
 * derived so it stays readable.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "theme.h"

#include "text.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

Theme theme;
static int current = -1;

const char *const THEME_NAMES[THEME_COUNT] = {
    "Emerald", "Pocket", "Kiwi", "Onyx", "Jade", "Sapphire", "Amethyst",
    "Ruby",    "Amber",  "Rose", "Custom"};

static const Theme THEMES[THEME_COUNT] = {
    [THEME_EMERALD] = {
        .bg = 0xff07110cu, .bg_deep = 0xff040b08u, .card = 0xff0d1f17u, .card_soft = 0xe60d1f17u,
        .row_selected = 0xff14382au, .focus = 0xff3fd08au, .pill = 0xff10291eu, .switch_on = 0xff35c980u,
        .switch_off = 0xff1e3a2eu, .text = 0xffe7f5edu, .text_dim = 0xff7fa593u, .text_soft = 0xffcbdcd2u,
        .hint = 0xff8fb3a1u, .gold = 0xffe3c36au, .danger = 0xffff8c7au, .good = 0xff57e39eu,
        .divider = 0xff17362au, .cover_outline = 0xff3fd08au, .backdrop_tint = 0xff1f8f62u,
        .backdrop = BACKDROP_STAGE, .layout = LAYOUT_CART,
        .font_regular = "fonts/Inter-400.ttf", .font_bold = "fonts/Inter-600.ttf"},
    [THEME_POCKET] = {
        .bg = 0xffedf3eeu, .bg_deep = 0xffdde8e1u, .card = 0xffffffffu, .card_soft = 0xf2ffffffu,
        .row_selected = 0xffcdebdcu, .focus = 0xff0e7c57u, .pill = 0xffe1ece6u, .switch_on = 0xff0e7c57u,
        .switch_off = 0xffc3d3c9u, .text = 0xff16241du, .text_dim = 0xff5c7267u, .text_soft = 0xff33473du,
        .hint = 0xff5c7267u, .gold = 0xff9a7a15u, .danger = 0xffb3372au, .good = 0xff127a50u,
        .divider = 0xffd2dfd7u, .cover_outline = 0xff0e7c57u, .backdrop_tint = 0xffcfe6dau,
        .backdrop = BACKDROP_FLAT, .layout = LAYOUT_CART, .light = true,
        .font_regular = "fonts/IBMPlexSans-400.ttf", .font_bold = "fonts/IBMPlexSans-600.ttf"},
    [THEME_KIWI] = {
        .bg = 0xff0a0f08u, .bg_deep = 0xff060905u, .card = 0xff13200fu, .card_soft = 0xe613200fu,
        .row_selected = 0xff1e3a14u, .focus = 0xffa6e05cu, .pill = 0xff18290fu, .switch_on = 0xff8fd44au,
        .switch_off = 0xff2a3f1cu, .text = 0xffeff6e6u, .text_dim = 0xff8aa17au, .text_soft = 0xffd5e4c6u,
        .hint = 0xff93ab83u, .gold = 0xffe8c45fu, .danger = 0xffff8b74u, .good = 0xff9ee063u,
        .divider = 0xff22351au, .cover_outline = 0xffa6e05cu, .backdrop_tint = 0xff5f9b32u,
        .backdrop = BACKDROP_STAGE, .layout = LAYOUT_CART,
        .font_regular = "fonts/ChakraPetch-500.ttf", .font_bold = "fonts/ChakraPetch-700.ttf"},
    [THEME_ONYX] = {
        .bg = 0xff080a09u, .bg_deep = 0xff040505u, .card = 0xff141715u, .card_soft = 0xe6141715u,
        .row_selected = 0xff1f2723u, .focus = 0xff2fbf87u, .pill = 0xff171a18u, .switch_on = 0xff2fbf87u,
        .switch_off = 0xff2c322fu, .text = 0xffe6ebe8u, .text_dim = 0xff868f8au, .text_soft = 0xffc6cfcau,
        .hint = 0xff8c958fu, .gold = 0xffc9b071u, .danger = 0xffcf8a80u, .good = 0xff5fc79bu,
        .divider = 0xff232925u, .cover_outline = 0xff2fbf87u, .backdrop_tint = 0xff3e5148u,
        .backdrop = BACKDROP_FLAT, .layout = LAYOUT_CART,
        .font_regular = "fonts/WorkSans-400.ttf", .font_bold = "fonts/Inter-600.ttf"},
    [THEME_JADE] = {
        .bg = 0xff08130fu, .bg_deep = 0xff050c09u, .card = 0xff10231bu, .card_soft = 0xe610231bu,
        .row_selected = 0xff1a3a2eu, .focus = 0xff54d6a6u, .pill = 0xff142b22u, .switch_on = 0xff44c795u,
        .switch_off = 0xff224034u, .text = 0xffe2f2eau, .text_dim = 0xff84a99au, .text_soft = 0xffc5ddd2u,
        .hint = 0xff8db3a4u, .gold = 0xffdcc07au, .danger = 0xffff9180u, .good = 0xff63e0b1u,
        .divider = 0xff1a372bu, .cover_outline = 0xff54d6a6u, .backdrop_tint = 0xff2a7f66u,
        .backdrop = BACKDROP_LAMP, .layout = LAYOUT_CART,
        .font_regular = "fonts/Inter-400.ttf", .font_bold = "fonts/Fraunces-600.ttf"},
    [THEME_SAPPHIRE] = {
        .bg = 0xff070d18u, .bg_deep = 0xff04070fu, .card = 0xff0d1a2au, .card_soft = 0xe60d1a2au,
        .row_selected = 0xff12283fu, .focus = 0xff4aa8ffu, .pill = 0xff0f2136u, .switch_on = 0xff3f9cf0u,
        .switch_off = 0xff1e3450u, .text = 0xffe8f1fbu, .text_dim = 0xff7d93abu, .text_soft = 0xffc8d8e8u,
        .hint = 0xff8aa0b8u, .gold = 0xffe3c36au, .danger = 0xffff8c7au, .good = 0xff5fd0a8u,
        .divider = 0xff152a42u, .cover_outline = 0xff4aa8ffu, .backdrop_tint = 0xff1f5f9fu,
        .backdrop = BACKDROP_STAGE, .layout = LAYOUT_CART,
        .font_regular = "fonts/Inter-400.ttf", .font_bold = "fonts/Inter-600.ttf"},
    [THEME_AMETHYST] = {
        .bg = 0xff0c0714u, .bg_deep = 0xff07040cu, .card = 0xff1a0f28u, .card_soft = 0xe61a0f28u,
        .row_selected = 0xff2c1843u, .focus = 0xffb98cffu, .pill = 0xff190f29u, .switch_on = 0xffa879f5u,
        .switch_off = 0xff33204du, .text = 0xfff0e9fbu, .text_dim = 0xff9a86b3u, .text_soft = 0xffd9ccecu,
        .hint = 0xffa794c0u, .gold = 0xffe8c45fu, .danger = 0xffff8c7au, .good = 0xff6fd6b0u,
        .divider = 0xff261638u, .cover_outline = 0xffb98cffu, .backdrop_tint = 0xff6a3fa0u,
        .backdrop = BACKDROP_STAGE, .layout = LAYOUT_CART,
        .font_regular = "fonts/Inter-400.ttf", .font_bold = "fonts/Inter-600.ttf"},
    [THEME_RUBY] = {
        .bg = 0xff12070au, .bg_deep = 0xff0a0406u, .card = 0xff261016u, .card_soft = 0xe6261016u,
        .row_selected = 0xff43202au, .focus = 0xffff6b7du, .pill = 0xff2a1017u, .switch_on = 0xfff2586au,
        .switch_off = 0xff4a2430u, .text = 0xfffbeaecu, .text_dim = 0xffb3898fu, .text_soft = 0xfff0d2d6u,
        .hint = 0xffbd9298u, .gold = 0xffe8c45fu, .danger = 0xffffb0a6u, .good = 0xff6fd6b0u,
        .divider = 0xff3a1d24u, .cover_outline = 0xffff6b7du, .backdrop_tint = 0xffa03040u,
        .backdrop = BACKDROP_LAMP, .layout = LAYOUT_CART,
        .font_regular = "fonts/Inter-400.ttf", .font_bold = "fonts/Inter-600.ttf"},
    [THEME_AMBER] = {
        .bg = 0xff120d05u, .bg_deep = 0xff0a0703u, .card = 0xff261b0cu, .card_soft = 0xe6261b0cu,
        .row_selected = 0xff42300fu, .focus = 0xffffb74au, .pill = 0xff2a1e0du, .switch_on = 0xfff0a83au,
        .switch_off = 0xff4a3617u, .text = 0xfffdf3e2u, .text_dim = 0xffb3a184u, .text_soft = 0xfff0e0c4u,
        .hint = 0xffbdaa8au, .gold = 0xffffd98au, .danger = 0xffff8c7au, .good = 0xff7fd6a0u,
        .divider = 0xff3a2b13u, .cover_outline = 0xffffb74au, .backdrop_tint = 0xffa86a20u,
        .backdrop = BACKDROP_LAMP, .layout = LAYOUT_CART,
        .font_regular = "fonts/Inter-400.ttf", .font_bold = "fonts/Inter-600.ttf"},
    [THEME_ROSE] = {
        .bg = 0xff120711u, .bg_deep = 0xff0a0409u, .card = 0xff26101fu, .card_soft = 0xe626101fu,
        .row_selected = 0xff42203au, .focus = 0xffff86c8u, .pill = 0xff2a1024u, .switch_on = 0xfff56ab0u,
        .switch_off = 0xff4a243cu, .text = 0xfffdeaf5u, .text_dim = 0xffb389a2u, .text_soft = 0xfff0d2e2u,
        .hint = 0xffbd92abu, .gold = 0xffe8c45fu, .danger = 0xffffb0a6u, .good = 0xff6fd6b0u,
        .divider = 0xff3a1d33u, .cover_outline = 0xffff86c8u, .backdrop_tint = 0xffa03070u,
        .backdrop = BACKDROP_STAGE, .layout = LAYOUT_CART,
        .font_regular = "fonts/Inter-400.ttf", .font_bold = "fonts/Inter-600.ttf"},
    /* the placeholder is never shown: theme_at() builds the real one from the
     * two colours in Settings, but it needs the font names in the table */
    [THEME_CUSTOM] = {
        .bg = 0xff07110cu, .bg_deep = 0xff040b08u, .card = 0xff0d1f17u, .card_soft = 0xe60d1f17u,
        .row_selected = 0xff14382au, .focus = 0xff3fd08au, .pill = 0xff10291eu, .switch_on = 0xff35c980u,
        .switch_off = 0xff1e3a2eu, .text = 0xffe7f5edu, .text_dim = 0xff7fa593u, .text_soft = 0xffcbdcd2u,
        .hint = 0xff8fb3a1u, .gold = 0xffe3c36au, .danger = 0xffff8c7au, .good = 0xff57e39eu,
        .divider = 0xff17362au, .cover_outline = 0xff3fd08au, .backdrop_tint = 0xff1f8f62u,
        .backdrop = BACKDROP_STAGE, .layout = LAYOUT_CART,
        .font_regular = "fonts/Inter-400.ttf", .font_bold = "fonts/Inter-600.ttf"},
};

/* ---------------------------------------------------------------- Custom */

#define CUSTOM_BG_DEFAULT 0x07110cu
#define CUSTOM_ACCENT_DEFAULT 0x3fd08au

static uint32_t custom_bg = CUSTOM_BG_DEFAULT;
static uint32_t custom_accent = CUSTOM_ACCENT_DEFAULT;
static Theme custom_theme;
static bool custom_built;

static float lum(uint32_t c)
{
    float r = (float)((c >> 16) & 0xffu) / 255.0f;
    float g = (float)((c >> 8) & 0xffu) / 255.0f;
    float b = (float)(c & 0xffu) / 255.0f;
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

/* Two colours mixed opaquely; t = 0 is a, t = 1 is b (0xRRGGBB in, out). */
static uint32_t mix(uint32_t a, uint32_t b, float t)
{
    if (t <= 0.0f)
        return a & 0xffffffu;
    if (t >= 1.0f)
        return b & 0xffffffu;
    uint32_t out = 0;
    for (int s = 0; s <= 16; s += 8)
    {
        float ca = (float)((a >> s) & 0xffu), cb = (float)((b >> s) & 0xffu);
        uint32_t c = (uint32_t)(ca + (cb - ca) * t + 0.5f);
        out |= (c > 255 ? 255 : c) << s;
    }
    return out;
}

/* A readable theme from just a ground and an accent. The accent is pushed
 * away from the ground when they are too close to tell apart, which is what
 * stops a #0a0a0a accent on a #0a0a0a background from vanishing. */
static void build_custom(void)
{
    Theme *t = &custom_theme;
    memset(t, 0, sizeof(*t));
    const uint32_t bg = custom_bg & 0xffffffu, ac = custom_accent & 0xffffffu;
    const bool light = lum(bg) > 0.5f;
    uint32_t focus = ac;
    if (fabsf(lum(focus) - lum(bg)) < 0.22f)
        focus = mix(focus, light ? 0x000000u : 0xffffffu, 0.40f);

    t->light = light;
    t->bg = 0xff000000u | bg;
    t->focus = 0xff000000u | focus;
    t->cover_outline = t->focus;
    t->switch_on = t->focus;
    t->good = t->focus;
    t->layout = LAYOUT_CART;
    t->backdrop = light ? BACKDROP_FLAT : BACKDROP_STAGE;
    t->backdrop_tint = 0xff000000u | mix(bg, focus, light ? 0.25f : 0.55f);
    t->font_regular = "fonts/Inter-400.ttf";
    t->font_bold = "fonts/Inter-600.ttf";

    if (light)
    {
        t->bg_deep = 0xff000000u | mix(bg, 0x000000u, 0.06f);
        t->card = 0xff000000u | mix(bg, 0xffffffu, 0.78f);
        t->card_soft = 0xf2000000u | (t->card & 0xffffffu);
        t->row_selected = 0xff000000u | mix(bg, focus, 0.26f);
        t->pill = 0xff000000u | mix(bg, focus, 0.10f);
        t->switch_off = 0xff000000u | mix(bg, 0x000000u, 0.12f);
        t->text = 0xff000000u | mix(0x000000u, focus, 0.10f);
        t->text_dim = 0xff000000u | mix(t->text & 0xffffffu, bg, 0.48f);
        t->text_soft = 0xff000000u | mix(t->text & 0xffffffu, bg, 0.20f);
        t->hint = t->text_dim;
        t->divider = 0xff000000u | mix(bg, 0x000000u, 0.14f);
        t->gold = 0xff9a7a15u;
        t->danger = 0xffb3372au;
    }
    else
    {
        t->bg_deep = 0xff000000u | mix(bg, 0x000000u, 0.35f);
        t->card = 0xff000000u | mix(mix(bg, 0xffffffu, 0.07f), focus, 0.04f);
        t->card_soft = 0xe6000000u | (t->card & 0xffffffu);
        t->row_selected = 0xff000000u | mix(bg, focus, 0.20f);
        t->pill = 0xff000000u | mix(bg, focus, 0.08f);
        t->switch_off = 0xff000000u | mix(bg, focus, 0.14f);
        t->text = 0xff000000u | mix(0xffffffu, focus, 0.06f);
        t->text_dim = 0xff000000u | mix(t->text & 0xffffffu, bg, 0.55f);
        t->text_soft = 0xff000000u | mix(t->text & 0xffffffu, bg, 0.25f);
        t->hint = t->text_dim;
        t->divider = 0xff000000u | mix(bg, focus, 0.12f);
        t->gold = 0xffe3c36au;
        t->danger = 0xffff8c7au;
    }
}

void theme_customize(uint32_t bg, uint32_t accent)
{
    custom_bg = bg & 0xffffffu;
    custom_accent = accent & 0xffffffu;
    custom_built = false;
    if (current == THEME_CUSTOM)
        theme_apply(THEME_CUSTOM); /* the whole interface changes on the next frame */
}

uint32_t theme_custom_bg(void)
{
    return custom_bg;
}

uint32_t theme_custom_accent(void)
{
    return custom_accent;
}

void theme_hex(uint32_t colour, char *buf, size_t size)
{
    if (!buf || size < 8)
    {
        if (buf && size)
            buf[0] = '\0';
        return;
    }
    snprintf(buf, size, "#%06X", colour & 0xffffffu);
}

bool theme_parse_hex(const char *text, uint32_t *out)
{
    if (!text)
        return false;
    if (*text == '#')
        ++text;
    uint32_t v = 0;
    int n = 0;
    for (; text[n] && n < 6; ++n)
    {
        char c = text[n];
        int d;
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'a' && c <= 'f')
            d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            d = c - 'A' + 10;
        else
            return false;
        v = (v << 4) | (uint32_t)d;
    }
    if (n != 6)
        return false;
    if (out)
        *out = v;
    return true;
}

/* ---------------------------------------------------------------- apply */

void theme_apply(int id)
{
    if (id < 0 || id >= THEME_COUNT)
        id = THEME_EMERALD;
    const Theme *src = id == THEME_CUSTOM ? theme_at(THEME_CUSTOM) : &THEMES[id];
    const char *old_regular = current >= 0 ? theme.font_regular : NULL;
    const char *old_bold = current >= 0 ? theme.font_bold : NULL;
    theme = *src;
    current = id;
    if (!old_regular || !old_bold || strcmp(old_regular, theme.font_regular) ||
        strcmp(old_bold, theme.font_bold))
        text_set_fonts(theme.font_regular, theme.font_bold);
}

int theme_current(void)
{
    return current;
}

const Theme *theme_at(int id)
{
    if (id == THEME_CUSTOM)
    {
        if (!custom_built)
        {
            build_custom();
            custom_built = true;
        }
        return &custom_theme;
    }
    if (id < 0 || id >= THEME_COUNT)
        id = THEME_EMERALD;
    return &THEMES[id];
}
