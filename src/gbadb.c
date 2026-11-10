/*
 * GBAR5 - what the shelf knows about a Game Boy Advance cartridge: the day it
 * came out, what it plays like, who made it, and how many can play at once.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * None of that is in a cartridge: its header holds a title, a four-letter game
 * code and a maker code, nothing more. So the shelf reads it from
 * assets/gba-gamedb.txt (built by tools/make-gba-gamedb.py from the LaunchBox
 * Games Database), once, when a game's details are first drawn.
 *
 * A cartridge is found by the name of the file it came from and by the title in
 * its header, compared without case, punctuation or accents, and without a
 * leading "the". Names that differ by a word or two still match, which is how a
 * catalogue's naming and a dump's differ: "Pokemon Emerald" finds "Pokémon
 * Emerald Version", and the released game beats a hack of it.
 *
 * <root>/gba-metadata.txt takes the same lines as the shipped list and wins
 * over it, for the homebrew and the hacks no database has heard of.
 */
#include "gbadb.h"

#include "app.h"
#include "platform/platform.h"
#include "psxs5.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_RECORDS 8192
#define FOLDED 160

typedef struct
{
    const char *name, *released, *genres, *developer, *publisher;
    int players;
    char kind; /* 'R' released, 'M' ROM hack, 'H' homebrew, 'U' unlicensed, 'X' other */
} Record;

static char *catalogue; /* assets/gba-gamedb.txt, held for the life of the program */
static char *mine;      /* <root>/gba-metadata.txt, when there is one */
static Record *records;
static int record_count;
static Record *own;
static int own_count;
static bool tried;

/* the game last asked about: drawing asks again for every frame */
static char memo_key[FOLDED * 2 + 32];
static const Record *memo_hit;
static bool memo_done;

/* ---------------------------------------------------------------- the file */

static char *read_all(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    long size = 0;
    if (fseek(f, 0, SEEK_END) == 0)
        size = ftell(f);
    if (size <= 0)
    {
        fclose(f);
        return NULL;
    }
    rewind(f);
    char *text = malloc((size_t)size + 1);
    if (text)
    {
        size_t got = fread(text, 1, (size_t)size, f);
        text[got] = '\0';
    }
    fclose(f);
    return text;
}

/* one line: name<TAB>released<TAB>genres<TAB>developer<TAB>publisher<TAB>players<TAB>kind */
static int parse(char *text, Record *into, int max)
{
    int n = 0;
    for (char *line = text; line && *line && n < max;)
    {
        char *next = strchr(line, '\n');
        if (next)
            *next++ = '\0';
        line[strcspn(line, "\r")] = '\0';
        if (line[0] != '#' && line[0])
        {
            char *field[7] = {0};
            char *p = line;
            for (int i = 0; i < 7 && p && *p; ++i)
            {
                field[i] = p;
                p = strchr(p, '\t');
                if (p)
                    *p++ = '\0';
            }
            if (field[0] && field[0][0])
            {
                Record *r = &into[n++];
                memset(r, 0, sizeof(*r));
                r->name = field[0];
                r->released = field[1] ? field[1] : "";
                r->genres = field[2] ? field[2] : "";
                r->developer = field[3] ? field[3] : "";
                r->publisher = field[4] ? field[4] : "";
                r->players = field[5] ? atoi(field[5]) : 0;
                r->kind = field[6] && field[6][0] ? field[6][0] : 'R';
            }
        }
        line = next;
    }
    return n;
}

static void load(void)
{
    tried = true;
    char path[PSXS5_PATH_MAX];
    plat_asset_path(path, sizeof(path), "gba-gamedb.txt");
    catalogue = read_all(path);
    if (catalogue)
    {
        records = malloc(sizeof(Record) * MAX_RECORDS);
        if (records)
            record_count = parse(catalogue, records, MAX_RECORDS);
        if (!record_count)
        {
            free(records);
            records = NULL;
        }
    }
    path_join(path, sizeof(path), app.paths.root, "gba-metadata.txt");
    mine = read_all(path);
    if (mine)
    {
        own = malloc(sizeof(Record) * MAX_RECORDS);
        if (own)
            own_count = parse(mine, own, MAX_RECORDS);
        if (own_count)
            psxs5_log("gbadb: %d cartridges known, plus %d from gba-metadata.txt", record_count, own_count);
        else
            psxs5_log("gbadb: gba-metadata.txt holds no lines the shelf can read");
    }
    else
        psxs5_log("gbadb: %d cartridges known", record_count);
}

/* ---------------------------------------------------------------- names */

/* "Pokémon Emerald (U)" -> "pokemon emerald u": lower case, accents folded to
 * the letters under them, punctuation gone, one space between words, and a
 * leading "the" dropped (a catalogue says "The Legend of Zelda", a dump says
 * "Legend of Zelda, The"). Returns the number of words. */
static int fold(const char *in, char *out, size_t size)
{
    size_t w = 0;
    bool space = false;
    for (size_t i = 0; in && in[i] && w + 1 < size;)
    {
        unsigned char c = (unsigned char)in[i];
        char add[4] = {0};
        if (c < 0x80)
        {
            ++i;
            if (c >= 'A' && c <= 'Z')
                add[0] = (char)(c - 'A' + 'a');
            else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
                add[0] = (char)c;
            else
                space = true;
        }
        else
        {
            /* Latin-1 supplement and Latin extended-A: the accents game names use */
            uint32_t cp = 0;
            int len = (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 1;
            if (len == 2)
                cp = (uint32_t)(c & 0x1f);
            else if (len == 3)
                cp = (uint32_t)(c & 0x0f);
            else if (len == 4)
                cp = (uint32_t)(c & 0x07);
            else
                cp = 0xfffd;
            bool bad = false;
            for (int k = 1; k < len; ++k)
            {
                unsigned char cc = (unsigned char)in[i + (size_t)k];
                if ((cc & 0xc0) != 0x80)
                {
                    bad = true;
                    break;
                }
                cp = (cp << 6) | (uint32_t)(cc & 0x3f);
            }
            if (bad)
            {
                space = true;
                ++i;
                continue;
            }
            i += (size_t)len;
            switch (cp)
            {
            /* À-Å à-å Æ æ Ç ç È-Ë è-ë Ì-Ï ì-ï Ð ð Ñ ñ */
            case 0xc0: case 0xc1: case 0xc2: case 0xc3: case 0xc4: case 0xc5:
            case 0xe0: case 0xe1: case 0xe2: case 0xe3: case 0xe4: case 0xe5:
                add[0] = 'a';
                break;
            case 0xc6: case 0xe6: strcpy(add, "ae"); break;
            case 0xc7: case 0xe7: add[0] = 'c'; break;
            case 0xc8: case 0xc9: case 0xca: case 0xcb:
            case 0xe8: case 0xe9: case 0xea: case 0xeb:
                add[0] = 'e';
                break;
            case 0xcc: case 0xcd: case 0xce: case 0xcf:
            case 0xec: case 0xed: case 0xee: case 0xef:
                add[0] = 'i';
                break;
            case 0xd0: case 0xf0: add[0] = 'd'; break;
            case 0xd1: case 0xf1: add[0] = 'n'; break;
            /* Ò-Ö Ø ò-ö ø Ù-Ü ù-ü Ý ý ÿ */
            case 0xd2: case 0xd3: case 0xd4: case 0xd5: case 0xd6: case 0xd8:
            case 0xf2: case 0xf3: case 0xf4: case 0xf5: case 0xf6: case 0xf8:
                add[0] = 'o';
                break;
            case 0xd9: case 0xda: case 0xdb: case 0xdc:
            case 0xf9: case 0xfa: case 0xfb: case 0xfc:
                add[0] = 'u';
                break;
            case 0xdd: case 0xfd: case 0xff: add[0] = 'y'; break;
            case 0xde: case 0xfe: strcpy(add, "th"); break;
            case 0xdf: strcpy(add, "ss"); break;
            default: space = true; break; /* what has no Latin under it is dropped */
            }
        }
        if (add[0])
        {
            if (space && w > 0 && out[w - 1] != ' ')
                out[w++] = ' ';
            space = false;
            for (size_t k = 0; add[k] && w + 1 < size; ++k)
                out[w++] = add[k];
        }
    }
    out[w] = '\0';
    while (w > 0 && out[w - 1] == ' ')
        out[--w] = '\0';

    /* a leading "the" carries no meaning: drop it, and the space after it */
    if (w > 3 && !strncmp(out, "the ", 4))
    {
        memmove(out, out + 4, w - 3);
        w -= 4;
    }
    int words = 0;
    for (size_t i = 0; i < w; ++i)
        if (out[i] != ' ' && (i == 0 || out[i - 1] == ' '))
            ++words;
    return words;
}

/* Is every word of `query` a word of `name` (or the start of one)? */
static bool has_words(const char *name, const char *query)
{
    for (const char *q = query; *q;)
    {
        while (*q == ' ')
            ++q;
        if (!*q)
            break;
        size_t len = strcspn(q, " ");
        bool found = false;
        for (const char *n = name; *n && !found;)
        {
            while (*n == ' ')
                ++n;
            if (!*n)
                break;
            size_t nlen = strcspn(n, " ");
            if (nlen >= len && !strncmp(n, q, len))
                found = true;
            n += nlen;
        }
        if (!found)
            return false;
        q += len;
    }
    return true;
}

/* How wrong a name that holds every word of the query is: the words it adds,
 * whether it is a hack rather than the game, and whether it has no date. */
static int wrongness(const Record *r, const char *query, int query_words)
{
    char name[FOLDED];
    int words = fold(r->name, name, sizeof(name));
    if (!has_words(name, query))
        return -1;
    int extra = words - query_words;
    if (extra < 0)
        extra = 0; /* "Legend of Zelda, The - ..." repeats a word the database doesn't */
    return extra * 4 + (r->kind == 'R' ? 0 : 2) + (r->released[0] ? 0 : 1);
}

static const Record *exact(const Record *list, int count, const char *query)
{
    for (int i = 0; i < count; ++i)
    {
        char name[FOLDED];
        fold(list[i].name, name, sizeof(name));
        if (!strcmp(name, query))
            return &list[i];
    }
    return NULL;
}

/* the closest name that holds every word of the query */
static const Record *closest(const Record *list, int count, const char *query, int query_words)
{
    const Record *best = NULL;
    int best_score = 0;
    for (int i = 0; i < count; ++i)
    {
        int score = wrongness(&list[i], query, query_words);
        if (score < 0 || (best && score >= best_score))
            continue;
        best = &list[i];
        best_score = score;
    }
    return best;
}

static const Record *lookup(const char *title, const char *header)
{
    char query[2][FOLDED];
    int words[2] = {0, 0};
    int n = 0;
    if (title && title[0])
        words[n++] = fold(title, query[0], sizeof(query[0]));
    if (header && header[0])
    {
        char folded[FOLDED];
        int w = fold(header, folded, sizeof(folded));
        if (w > 0 && (n == 0 || strcmp(folded, query[0])))
        {
            str_copy(query[n], sizeof(query[n]), folded);
            words[n++] = w;
        }
    }

    /* the name exactly as the database spells it, in either list */
    for (int q = 0; q < n; ++q)
    {
        const Record *hit = own ? exact(own, own_count, query[q]) : NULL;
        if (!hit && records)
            hit = exact(records, record_count, query[q]);
        if (hit)
            return hit;
    }
    /* then the nearest name, the game before its hacks */
    for (int q = 0; q < n; ++q)
    {
        if (strlen(query[q]) < 4)
            continue;
        const Record *hit = own ? closest(own, own_count, query[q], words[q]) : NULL;
        if (!hit && records)
            hit = closest(records, record_count, query[q], words[q]);
        if (hit)
            return hit;
    }
    return NULL;
}

/* ---------------------------------------------------------------- the answer */

bool gbadb_get(const Game *g, GameInfo *out)
{
    if (!g || !out)
        return false;
    if (!tried)
        load();
    if (!records && !own)
        return false;

    const char *title = g->title;
    const char *header = g->rom_title;
    char key[FOLDED * 2 + 32];
    snprintf(key, sizeof(key), "%s\n%s", title ? title : "", header ? header : "");
    if (!memo_done || strcmp(key, memo_key))
    {
        str_copy(memo_key, sizeof(memo_key), key);
        memo_hit = lookup(title, header);
        memo_done = true;
    }
    if (!memo_hit)
        return false;

    const Record *r = memo_hit;
    memset(out, 0, sizeof(*out));
    str_copy(out->name, sizeof(out->name), r->name);
    str_copy(out->released, sizeof(out->released), r->released);
    str_copy(out->genre, sizeof(out->genre), r->genres);
    str_copy(out->developer, sizeof(out->developer), r->developer);
    str_copy(out->publisher, sizeof(out->publisher), r->publisher);
    out->year = atoi(r->released);
    out->min_players = r->players > 0 ? 1 : 0;
    out->max_players = r->players;
    return true;
}
