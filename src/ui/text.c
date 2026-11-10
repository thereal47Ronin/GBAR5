/*
 * GBAR5 - TrueType text (Inter) on top of the platform mesh API.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Glyphs for Latin-1 are packed into one atlas per (weight, size bucket) the
 * first time that size is used; a line of text is then a single mesh draw.
 * Japanese, Chinese and Korean: the characters the current language's strings
 * use are packed into the same atlases from the Noto subsets (see i18n.c).
 */
#include "text.h"

#include "../i18n.h"

#include "../platform/platform.h"
#include "stb_truetype.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FIRST_CP 32
#define CP_COUNT 224 /* U+0020..U+00FF */
#define MAX_ATLASES 40
#define MAX_EXTRA 2048
#define EXTRA_MAX_PX 56 /* above this the scripts' glyphs are not packed */

typedef struct
{
    int weight, px;
    int width, height;
    float ascent;
    uint32_t last_use;
    stbtt_packedchar chars[CP_COUNT];
    stbtt_packedchar *extra; /* parallel to extras[] */
    PlatTexture *texture;
} Atlas;

static unsigned char *font_data[3]; /* regular, bold (the theme's), mono */
static stbtt_fontinfo font_info[3];
static bool font_ok[3];
static Atlas atlases[MAX_ATLASES];
static int atlas_count;
static uint32_t use_tick;

/* Faces for what Inter cannot draw: Japanese, Chinese, Korean. The current
 * language's script is tried first. */
enum
{
    FB_JP = 0,
    FB_ZH,
    FB_KR,
    FB_COUNT
};
static const char *const FB_FILES[FB_COUNT] = {"fonts/NotoSansJP-PSXS5.ttf",
                                               "fonts/NotoSansSC-PSXS5.ttf",
                                               "fonts/NotoSansKR-PSXS5.ttf"};
static unsigned char *fb_data[FB_COUNT];
static stbtt_fontinfo fb_info[FB_COUNT];
static bool fb_ok[FB_COUNT];

/* Characters beyond Latin-1 that the current language needs, sorted, with the
 * face that draws each one. */
typedef struct
{
    int cp;
    int8_t face;
} Extra;

static Extra extras[MAX_EXTRA];
static int extra_count;
static bool extra_dirty = true;

static const int buckets[] = {16, 20, 24, 28, 32, 40, 48, 56, 64, 80, 96, 128};

static unsigned char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *data = size > 0 ? malloc((size_t)size) : NULL;
    if (data && fread(data, 1, (size_t)size, f) != (size_t)size)
    {
        free(data);
        data = NULL;
    }
    fclose(f);
    return data;
}

static void free_atlases(void);

/* The interface's two weights (the theme's); Inter when a file is missing. */
static bool load_fonts(const char *regular, const char *bold)
{
    const char *files[2] = {regular, bold};
    for (int w = 0; w < 2; ++w)
    {
        char path[PSXS5_PATH_MAX];
        plat_asset_path(path, sizeof(path), files[w]);
        unsigned char *data = read_file(path);
        stbtt_fontinfo info;
        bool ok = data && stbtt_InitFont(&info, data, stbtt_GetFontOffsetForIndex(data, 0));
        if (!ok)
        {
            psxs5_log("font missing or invalid: %s", path);
            free(data);
            if (font_ok[w])
                continue; /* keep the current one */
            plat_asset_path(path, sizeof(path), w ? "fonts/Inter-600.ttf" : "fonts/Inter-400.ttf");
            data = read_file(path);
            ok = data && stbtt_InitFont(&info, data, stbtt_GetFontOffsetForIndex(data, 0));
            if (!ok)
            {
                free(data);
                continue;
            }
        }
        free(font_data[w]);
        font_data[w] = data;
        font_info[w] = info;
        font_ok[w] = true;
    }
    if (!font_ok[FONT_BOLD] && font_ok[FONT_REGULAR])
    {
        font_info[FONT_BOLD] = font_info[FONT_REGULAR];
        font_ok[FONT_BOLD] = true;
    }
    return font_ok[FONT_REGULAR];
}

void text_set_fonts(const char *regular, const char *bold)
{
    load_fonts(regular, bold);
    free_atlases(); /* glyphs are packed again with the new faces */
    extra_dirty = true;
}

bool text_init(void)
{
    load_fonts("fonts/Inter-400.ttf", "fonts/Inter-600.ttf");
    char mono_path[PSXS5_PATH_MAX];
    plat_asset_path(mono_path, sizeof(mono_path), "fonts/IBMPlexMono-400.ttf");
    font_data[FONT_MONO] = read_file(mono_path);
    font_ok[FONT_MONO] = font_data[FONT_MONO] &&
                         stbtt_InitFont(&font_info[FONT_MONO], font_data[FONT_MONO],
                                        stbtt_GetFontOffsetForIndex(font_data[FONT_MONO], 0));
    if (!font_ok[FONT_MONO])
    {
        psxs5_log("font missing or invalid: %s (guides use the regular face)", mono_path);
        free(font_data[FONT_MONO]);
        font_data[FONT_MONO] = NULL;
    }
    for (int f = 0; f < FB_COUNT; ++f)
    {
        char fb_path[PSXS5_PATH_MAX];
        plat_asset_path(fb_path, sizeof(fb_path), FB_FILES[f]);
        fb_data[f] = read_file(fb_path);
        fb_ok[f] = fb_data[f] &&
                   stbtt_InitFont(&fb_info[f], fb_data[f], stbtt_GetFontOffsetForIndex(fb_data[f], 0));
        if (!fb_ok[f])
            psxs5_log("font missing or invalid: %s (its script will show as ?)", fb_path);
    }
    return font_ok[FONT_REGULAR];
}

static void free_atlases(void)
{
    for (int i = 0; i < atlas_count; ++i)
    {
        plat_texture_free(atlases[i].texture);
        free(atlases[i].extra);
        atlases[i].extra = NULL;
        atlases[i].texture = NULL;
    }
    atlas_count = 0;
    use_tick = 0;
}

void text_shutdown(void)
{
    free_atlases();
    for (int w = 0; w < 3; ++w)
        free(font_data[w]);
    for (int f = 0; f < FB_COUNT; ++f)
        free(fb_data[f]);
}

static int decode(const char **p);

static int cmp_extra(const void *a, const void *b)
{
    const Extra *x = a, *y = b;
    return x->cp - y->cp;
}

/* The face order for the current language: its own script first, so a Chinese
 * character is drawn with the Chinese shapes rather than the Japanese ones. */
static int face_order(int *order)
{
    int lang = i18n_get();
    int first = lang == LANG_ZH ? FB_ZH : lang == LANG_KO ? FB_KR : FB_JP;
    int n = 0;
    order[n++] = first;
    for (int f = 0; f < FB_COUNT; ++f)
        if (f != first)
            order[n++] = f;
    return n;
}

static void add_extra(int cp, const int *order, int faces)
{
    if (cp <= 0xFF || extra_count == MAX_EXTRA)
        return;
    for (int i = 0; i < extra_count; ++i)
        if (extras[i].cp == cp)
            return;
    for (int f = 0; f < faces; ++f)
        if (fb_ok[order[f]] && stbtt_FindGlyphIndex(&fb_info[order[f]], cp))
        {
            extras[extra_count].cp = cp;
            extras[extra_count].face = (int8_t)order[f];
            ++extra_count;
            return;
        }
}

/* The non-Latin-1 characters the current language needs. The selector draws
 * every language's own name, so those are always included too. */
static void collect_extra(void)
{
    extra_dirty = false;
    extra_count = 0;
    int order[FB_COUNT];
    int faces = face_order(order);
    bool any = false;
    for (int f = 0; f < faces; ++f)
        any = any || fb_ok[order[f]];
    if (!any)
        return;
    int lang = i18n_get();
    for (int i = 0;; ++i)
    {
        const char *s = i18n_string(lang, i);
        if (!s)
            break;
        while (*s)
        {
            int cp = decode(&s);
            add_extra(cp, order, faces);
        }
    }
    qsort(extras, (size_t)extra_count, sizeof(extras[0]), cmp_extra);
}

void text_language_changed(void)
{
    free_atlases();
    extra_dirty = true;
    collect_extra();
    psxs5_log("language %d: %d characters beyond Latin-1 to pack", i18n_get(), extra_count);
}

void text_log_fonts(void)
{
    psxs5_log("fonts: interface %s, mono %s, JP %s, SC %s, KR %s",
              font_ok[FONT_REGULAR] ? "ok" : "MISSING", font_ok[FONT_MONO] ? "ok" : "MISSING",
              fb_ok[FB_JP] ? "ok" : "MISSING", fb_ok[FB_ZH] ? "ok" : "MISSING",
              fb_ok[FB_KR] ? "ok" : "MISSING");
    collect_extra(); /* the count is per language, so it is only known once asked for */
    psxs5_log("language %d: %d characters beyond Latin-1 to pack", i18n_get(), extra_count);
}

/* Fallback glyphs are drawn at Inter's em size for the same pixel height. */
static float fb_em_px(const stbtt_fontinfo *main, float px)
{
    return stbtt_ScaleForPixelHeight(main, px) / stbtt_ScaleForMappingEmToPixels(main, 1.0f);
}

static int bucket_for(float size)
{
    for (size_t i = 0; i < sizeof(buckets) / sizeof(buckets[0]); ++i)
        if (buckets[i] >= size - 0.5f)
            return buckets[i];
    return buckets[sizeof(buckets) / sizeof(buckets[0]) - 1];
}

static Atlas *get_atlas(int weight, float size)
{
    weight = weight == FONT_MONO && font_ok[FONT_MONO] ? FONT_MONO : weight ? FONT_BOLD : FONT_REGULAR;
    if (!font_ok[weight])
        return NULL;
    int px = bucket_for(size);
    for (int i = 0; i < atlas_count; ++i)
        if (atlases[i].weight == weight && atlases[i].px == px)
        {
            atlases[i].last_use = ++use_tick;
            return &atlases[i];
        }
    if (extra_dirty)
        collect_extra();

    Atlas *a;
    if (atlas_count < MAX_ATLASES)
        a = &atlases[atlas_count++];
    else
    {
        /* every slot is taken: reuse the one nobody has asked for in longest */
        int oldest = 0;
        for (int i = 1; i < atlas_count; ++i)
            if (atlases[i].last_use < atlases[oldest].last_use)
                oldest = i;
        a = &atlases[oldest];
        plat_texture_free(a->texture);
        free(a->extra);
        a->texture = NULL;
        a->extra = NULL;
    }
    int over = px <= 32 ? 2 : 1;
    a->width = px <= 32 ? 512 : px <= 64 ? 1024 : 2048;
    a->height = px <= 48 ? 512 : 1024;
    if (extra_count && px <= EXTRA_MAX_PX)
    {
        /* room for the scripts' glyphs too: a square cell each, plus slack */
        double cell = px * 1.08 + 2.0;
        double need = (CP_COUNT * 0.55 + extra_count) * cell * cell * 1.35 * over * over;
        while ((double)a->width * a->height < need && (a->width < 4096 || a->height < 4096))
        {
            if (a->height < a->width)
                a->height *= 2;
            else
                a->width *= 2;
        }
        a->extra = calloc((size_t)extra_count, sizeof(stbtt_packedchar));
        if (!a->extra)
            return NULL;
    }
    unsigned char *alpha = calloc((size_t)a->width * a->height, 1);
    if (!alpha)
        return NULL;
    stbtt_pack_context pc;
    stbtt_PackBegin(&pc, alpha, a->width, a->height, 0, 1, NULL);
    stbtt_PackSetOversampling(&pc, (unsigned)over, 1);
    stbtt_PackFontRange(&pc, font_data[weight] ? font_data[weight] : font_data[0], 0,
                        (float)px, FIRST_CP, CP_COUNT, a->chars);
    if (a->extra)
    {
        int *cps = malloc(sizeof(int) * (size_t)extra_count);
        int *slot = malloc(sizeof(int) * (size_t)extra_count);
        stbtt_packedchar *tmp = malloc(sizeof(stbtt_packedchar) * (size_t)extra_count);
        if (cps && slot && tmp)
            for (int f = 0; f < FB_COUNT; ++f)
            {
                if (!fb_ok[f])
                    continue;
                int n = 0;
                for (int i = 0; i < extra_count; ++i)
                    if (extras[i].face == f)
                    {
                        cps[n] = extras[i].cp;
                        slot[n] = i;
                        ++n;
                    }
                if (!n)
                    continue;
                stbtt_pack_range r = {0};
                r.font_size = STBTT_POINT_SIZE(fb_em_px(&font_info[weight], (float)px));
                r.array_of_unicode_codepoints = cps;
                r.num_chars = n;
                r.chardata_for_range = tmp;
                if (!stbtt_PackFontRanges(&pc, fb_data[f], 0, &r, 1))
                    psxs5_log("atlas %dpx: %d %s glyphs did not fit", px, n, FB_FILES[f]);
                for (int k = 0; k < n; ++k)
                    a->extra[slot[k]] = tmp[k];
            }
        free(cps);
        free(slot);
        free(tmp);
    }
    stbtt_PackEnd(&pc);

    uint8_t *rgba = malloc((size_t)a->width * a->height * 4);
    if (!rgba)
    {
        free(alpha);
        return NULL;
    }
    for (int i = 0; i < a->width * a->height; ++i)
    {
        rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = alpha[i];
    }
    a->texture = plat_texture_create(rgba, a->width, a->height, true);
    free(rgba);
    free(alpha);

    int asc, desc, gap;
    stbtt_GetFontVMetrics(&font_info[weight], &asc, &desc, &gap);
    a->ascent = asc * stbtt_ScaleForPixelHeight(&font_info[weight], (float)px);
    a->weight = weight;
    a->px = px;
    ++atlas_count;
    return a;
}

/* Decodes one UTF-8 sequence. */
static int decode(const char **p)
{
    const unsigned char *s = (const unsigned char *)*p;
    int cp, len;
    if (s[0] < 0x80)
        cp = s[0], len = 1;
    else if ((s[0] & 0xe0) == 0xc0 && s[1])
        cp = ((s[0] & 0x1f) << 6) | (s[1] & 0x3f), len = 2;
    else if ((s[0] & 0xf0) == 0xe0 && s[1] && s[2])
        cp = ((s[0] & 0x0f) << 12) | ((s[1] & 0x3f) << 6) | (s[2] & 0x3f), len = 3;
    else if ((s[0] & 0xf8) == 0xf0 && s[1] && s[2] && s[3])
        cp = ((s[0] & 0x07) << 18) | ((s[1] & 0x3f) << 12) | ((s[2] & 0x3f) << 6) | (s[3] & 0x3f),
        len = 4;
    else
        cp = s[0], len = 1; /* stray Latin-1 byte */
    *p += len;
    return cp;
}

static int next_cp(const char **p)
{
    return decode(p);
}

/* The packed glyph for a character: Latin-1, then the Japanese set, else '?'. */
static const stbtt_packedchar *atlas_glyph(const Atlas *a, int cp, const stbtt_packedchar **base,
                                           int *index)
{
    if (cp >= FIRST_CP && cp < FIRST_CP + CP_COUNT)
    {
        *base = a->chars;
        *index = cp - FIRST_CP;
        return &a->chars[*index];
    }
    if (a->extra && extra_count)
    {
        Extra key = {cp, 0};
        const Extra *hit = bsearch(&key, extras, (size_t)extra_count, sizeof(extras[0]), cmp_extra);
        if (hit)
        {
            *base = a->extra;
            *index = (int)(hit - extras);
            return &a->extra[*index];
        }
    }
    *base = a->chars;
    *index = '?' - FIRST_CP;
    return &a->chars[*index];
}

float text_width(float size, int weight, const char *s)
{
    Atlas *a = get_atlas(weight, size);
    if (!a)
        return 0.0f;
    float scale = size / (float)a->px, w = 0.0f;
    while (*s)
    {
        const stbtt_packedchar *base;
        int index;
        w += atlas_glyph(a, next_cp(&s), &base, &index)->xadvance;
    }
    return w * scale;
}

float text_ink_left(float size, int weight)
{
    Atlas *a = get_atlas(weight, size);
    if (!a)
        return 0.0f;
    const stbtt_packedchar *base;
    int index;
    const stbtt_packedchar *g = atlas_glyph(a, 'L', &base, &index);
    return g->xoff * (size / (float)a->px);
}

void text_draw(float x, float y, float size, int weight, uint32_t argb, int align, const char *s)
{
    Atlas *a = get_atlas(weight, size);
    if (!a || !a->texture || !*s)
        return;
    if (align != ALIGN_LEFT)
    {
        float w = text_width(size, weight, s);
        x -= align == ALIGN_CENTER ? w * 0.5f : w;
    }
    /* One blit per glyph: the software renderer's copy path is far cheaper
     * than textured triangles. */
    float scale = size / (float)a->px, pen = 0.0f;
    while (*s)
    {
        int cp = next_cp(&s);
        const stbtt_packedchar *base;
        int index;
        const stbtt_packedchar *g = atlas_glyph(a, cp, &base, &index);
        if (g->x1 > g->x0 && g->y1 > g->y0)
            plat_draw_texture_region(a->texture, g->x0, g->y0, g->x1 - g->x0, g->y1 - g->y0,
                                     x + (pen + g->xoff) * scale, y + (a->ascent + g->yoff) * scale,
                                     (g->xoff2 - g->xoff) * scale, (g->yoff2 - g->yoff) * scale,
                                     argb);
        pen += g->xadvance;
    }
}

void text_draw_fit(float x, float y, float size, int weight, uint32_t argb, int align,
                   float max_width, const char *s)
{
    if (text_width(size, weight, s) <= max_width)
    {
        text_draw(x, y, size, weight, argb, align, s);
        return;
    }
    char buf[256];
    size_t len = strlen(s);
    if (len >= sizeof(buf) - 4)
        len = sizeof(buf) - 4;
    while (len > 0)
    {
        --len;
        while (len > 0 && ((unsigned char)s[len] & 0xc0) == 0x80)
            --len; /* stay on a UTF-8 boundary */
        memcpy(buf, s, len);
        strcpy(buf + len, "...");
        if (text_width(size, weight, buf) <= max_width)
            break;
    }
    text_draw(x, y, size, weight, argb, align, buf);
}

/* ---------------------------------------------------------------- CPU rendering */

/* Inter for Latin-1, the script's face for what it has (at Inter's em size). */
static const stbtt_fontinfo *cpu_font(const stbtt_fontinfo *f, float scale, int *cp,
                                      float *glyph_scale)
{
    *glyph_scale = scale;
    if (*cp <= 0xff)
        return f;
    int order[FB_COUNT];
    int faces = face_order(order);
    for (int i = 0; i < faces; ++i)
    {
        int fb = order[i];
        if (fb_ok[fb] && stbtt_FindGlyphIndex(&fb_info[fb], *cp))
        {
            *glyph_scale = stbtt_ScaleForMappingEmToPixels(
                &fb_info[fb], scale / stbtt_ScaleForMappingEmToPixels(f, 1.0f));
            return &fb_info[fb];
        }
    }
    *cp = '?';
    return f;
}

static float cpu_line_width(const stbtt_fontinfo *f, float scale, const char *s, size_t n)
{
    float w = 0.0f;
    const char *end = s + n;
    while (s < end && *s)
    {
        int adv, lsb, cp = next_cp(&s);
        float gs;
        const stbtt_fontinfo *g = cpu_font(f, scale, &cp, &gs);
        stbtt_GetCodepointHMetrics(g, cp, &adv, &lsb);
        w += adv * gs;
    }
    return w;
}

static void cpu_draw_line(uint8_t *rgba, int width, int height, float x, int baseline,
                          const stbtt_fontinfo *f, float scale, uint32_t argb, const char *s,
                          size_t n)
{
    const char *end = s + n;
    uint8_t cr = (argb >> 16) & 0xff, cg = (argb >> 8) & 0xff, cb = argb & 0xff;
    while (s < end && *s)
    {
        int cp = next_cp(&s);
        int adv, lsb, x0, y0, x1, y1;
        float gs;
        const stbtt_fontinfo *g = cpu_font(f, scale, &cp, &gs);
        stbtt_GetCodepointHMetrics(g, cp, &adv, &lsb);
        stbtt_GetCodepointBitmapBox(g, cp, gs, gs, &x0, &y0, &x1, &y1);
        int gw = x1 - x0, gh = y1 - y0;
        if (gw > 0 && gh > 0 && gw < 512 && gh < 512)
        {
            static unsigned char glyph[512 * 512];
            stbtt_MakeCodepointBitmap(g, glyph, gw, gh, gw, gs, gs, cp);
            for (int gy = 0; gy < gh; ++gy)
                for (int gx = 0; gx < gw; ++gx)
                {
                    int px = (int)x + x0 + gx, py = baseline + y0 + gy;
                    if (px < 0 || py < 0 || px >= width || py >= height)
                        continue;
                    unsigned a = glyph[gy * gw + gx] * (argb >> 24) / 255;
                    uint8_t *d = &rgba[(py * width + px) * 4];
                    d[0] = (uint8_t)((cr * a + d[0] * (255 - a)) / 255);
                    d[1] = (uint8_t)((cg * a + d[1] * (255 - a)) / 255);
                    d[2] = (uint8_t)((cb * a + d[2] * (255 - a)) / 255);
                }
        }
        x += adv * gs;
    }
}

int text_render_rgba(uint8_t *rgba, int width, int height, int y, float size, int weight,
                     uint32_t argb, int max_width, const char *s)
{
    weight = weight ? FONT_BOLD : FONT_REGULAR;
    if (!font_ok[weight])
        return 0;
    const stbtt_fontinfo *f = &font_info[weight];
    float scale = stbtt_ScaleForPixelHeight(f, size);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(f, &asc, &desc, &gap);
    int line_h = (int)((asc - desc + gap) * scale);
    int start_y = y;

    while (*s)
    {
        /* longest run of words that fits */
        size_t best = 0, i = 0;
        while (s[i])
        {
            size_t j = i;
            while (s[j] && s[j] != ' ')
                ++j;
            if (best > 0 && cpu_line_width(f, scale, s, j) > max_width)
                break;
            best = j;
            if (!s[j])
                break;
            i = j + 1;
        }
        if (best == 0)
            best = strlen(s);
        float lw = cpu_line_width(f, scale, s, best);
        cpu_draw_line(rgba, width, height, (width - lw) * 0.5f, y + (int)(asc * scale), f, scale,
                      argb, s, best);
        y += line_h;
        s += best;
        while (*s == ' ')
            ++s;
        if (y + line_h > height)
            break;
    }
    return y - start_y;
}
