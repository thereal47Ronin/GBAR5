/*
 * PSXS5 - game library scanning.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "library.h"

#include "disc.h"
#include "netfs.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DIR_FILES 64

/* Loadable images. A GBA cartridge is one file; nothing to choose between. */
static int image_rank(const char *name)
{
    const char *ext = path_ext(name);
    if (str_icmp(ext, "gba") == 0)
        return 2;
    if (str_icmp(ext, "agb") == 0)
        return 1; /* some dumps carry the older extension */
    return 0;
}

static bool contains_icase(const char *haystack, const char *needle)
{
    size_t n = strlen(needle);
    for (; *haystack; ++haystack)
    {
        size_t i = 0;
        while (i < n && haystack[i] &&
               tolower((unsigned char)haystack[i]) == tolower((unsigned char)needle[i]))
            ++i;
        if (i == n)
            return true;
    }
    return false;
}

/* No-Intro names carry the region in brackets: "(USA, Europe)", "(Japan)".
 * The shelf's region filter reads it from the serial field, so the first
 * region named becomes "U", "E" or "J". */
static void extract_region(const char *name, char *serial, size_t size)
{
    serial[0] = '\0';
    if (strstr(name, "(USA") || strstr(name, "(U)") || strstr(name, "(World"))
        str_copy(serial, size, "U");
    else if (strstr(name, "(Europe") || strstr(name, "(E)"))
        str_copy(serial, size, "E");
    else if (strstr(name, "(Japan") || strstr(name, "(J)"))
        str_copy(serial, size, "J");
}

/* "Resident Evil 2 [U] [SLUS-00421]" -> "Resident Evil 2"
 * "Chrono Cross (USA) (Disc 1)"      -> "Chrono Cross (USA)" */
static void clean_title(const char *raw, char *out, size_t size)
{
    char buf[256];
    str_copy(buf, sizeof(buf), raw);
    char *dot = strrchr(buf, '.');
    if (dot && image_rank(buf) > 0)
        *dot = '\0';

    char result[256];
    size_t w = 0;
    for (const char *p = buf; *p && w + 1 < sizeof(result);)
    {
        if (*p == '[')
        {
            const char *end = strchr(p, ']');
            if (end)
            {
                p = end + 1;
                continue;
            }
        }
        if (*p == '(')
        {
            const char *end = strchr(p, ')');
            if (end)
            {
                p = end + 1;
                continue;
            }
        }
        result[w++] = (*p == '_') ? ' ' : *p;
        ++p;
    }
    result[w] = '\0';

    /* collapse whitespace */
    char *src = result, *dst = result;
    bool space = true;
    for (; *src; ++src)
    {
        if (isspace((unsigned char)*src))
        {
            if (!space)
                *dst++ = ' ';
            space = true;
        }
        else
        {
            *dst++ = *src;
            space = false;
        }
    }
    while (dst > result && dst[-1] == ' ')
        --dst;
    *dst = '\0';
    str_copy(out, size, result[0] ? result : raw);
}

/* The title and game code a Game Boy Advance cartridge carries in its first
 * 0xC0 bytes (title at 0xA0, code at 0xAC). The title is what the publisher
 * called the game - often shorter than the catalogue's name - and is the surest
 * way to know which game a renamed dump is. Japanese cartridges hold Shift-JIS
 * there, so anything that isn't plain ASCII is left out. Read from local files
 * only: over a share this would pull the whole cartridge to find twelve
 * characters, and the file's name is a good enough key there. */
static void read_gba_header(Game *g)
{
    if (netfs_is_url(g->path))
        return;
    VFile *f = vf_open(g->path);
    if (!f)
        return;
    unsigned char head[0xc0];
    size_t got = vf_read(f, head, sizeof(head));
    vf_close(f);
    if (got < sizeof(head))
        return;

    const unsigned char *code = head + 0xac;
    bool code_ok = true;
    for (int i = 0; i < 4; ++i)
        if (!((code[i] >= 'A' && code[i] <= 'Z') || (code[i] >= 'a' && code[i] <= 'z') ||
              (code[i] >= '0' && code[i] <= '9')))
            code_ok = false;
    if (!code_ok)
        return;
    memcpy(g->code, code, 4);
    g->code[4] = '\0';

    /* the title is padded with noughts or spaces */
    size_t n = 0;
    while (n < 12 && head[0xa0 + n] != '\0' && head[0xa0 + n] != 0xff)
    {
        unsigned char c = head[0xa0 + n];
        if (c < 0x20 || c > 0x7e)
            return; /* not ASCII: leave the name the file gave it */
        g->rom_title[n++] = (char)c;
    }
    while (n > 0 && g->rom_title[n - 1] == ' ')
        --n;
    g->rom_title[n] = '\0';
}

/* Serial from the disc itself (exact), else serial.txt written by the sync
 * tool (needed for .chd), else whatever the file name carried. Then the id. */
static void finish_game(Game *g)
{
    char serial[16];
    char txt[PSXS5_PATH_MAX];
    const char *ext = path_ext(g->path);
    if (str_icmp(ext, "gba") == 0 || str_icmp(ext, "agb") == 0)
    {
        /* GBA cartridges carry no serial. The ROM's file name is the stable
         * key: it is also what libretro-thumbnails calls the box art. */
        read_gba_header(g);
        const char *base = strrchr(g->path, '/');
        base = base ? base + 1 : g->path;
        size_t n = strlen(base);
        const char *dot = strrchr(base, '.');
        if (dot)
            n = (size_t)(dot - base);
        if (n >= sizeof(g->id))
            n = sizeof(g->id) - 1;
        memcpy(g->id, base, n);
        g->id[n] = '\0';
        return;
    }
    /* on a network share the serial comes from the names: opening every disc
     * there took minutes for a big library */
    if (netfs_is_url(g->path))
        ;
    else if (disc_read_serial(g->path, serial, sizeof(serial)))
        str_copy(g->serial, sizeof(g->serial), serial);
    else
    {
        path_join(txt, sizeof(txt), g->folder, "serial.txt");
        VFile *f = vf_open(txt);
        if (f)
        {
            char line[64] = "";
            if (vf_gets(line, sizeof(line), f) && disc_format_serial(line, serial, sizeof(serial)))
                str_copy(g->serial, sizeof(g->serial), serial);
            vf_close(f);
        }
    }

    if (g->serial[0])
    {
        str_copy(g->id, sizeof(g->id), g->serial);
        return;
    }
    psxs5_log("library: no serial found in %s (put it in the file or folder name, or a serial.txt beside it)",
              g->path);
    size_t w = 0;
    for (const char *p = g->title; *p && w + 1 < sizeof(g->id); ++p)
        g->id[w++] = isalnum((unsigned char)*p) ? *p : '_';
    g->id[w] = '\0';
}

static Game *add_game(Library *lib)
{
    if (lib->count == lib->capacity)
    {
        int cap = lib->capacity ? lib->capacity * 2 : 64;
        Game *grown = realloc(lib->games, (size_t)cap * sizeof(Game));
        if (!grown)
            return NULL;
        lib->games = grown;
        lib->capacity = cap;
    }
    Game *g = &lib->games[lib->count++];
    memset(g, 0, sizeof(*g));
    return g;
}

static void set_disc_name(Game *g, const char *file)
{
    str_copy(g->disc_name, sizeof(g->disc_name), file);
    char *dot = strrchr(g->disc_name, '.');
    if (dot)
        *dot = '\0';
}

static int cmp_names(const void *a, const void *b)
{
    return str_icmp((const char *)a, (const char *)b);
}

typedef char NameBuf[256];

/* Reads up to MAX_DIR_FILES loadable entries of `dir`, sorted by name. */
static int list_images(const char *dir, NameBuf *names)
{
    VDir *d = vd_open(dir); /* a local folder or one on a network share */
    if (!d)
        return 0;
    int n = 0;
    const char *name;
    while ((name = vd_next(d, NULL)) && n < MAX_DIR_FILES)
    {
        if (name[0] == '.' || image_rank(name) == 0)
            continue;
        str_copy(names[n++], sizeof(NameBuf), name);
    }
    vd_close(d);
    qsort(names, (size_t)n, sizeof(NameBuf), cmp_names);
    return n;
}

static bool write_m3u(const char *path, NameBuf *discs, int count)
{
    if (netfs_is_url(path))
        return false; /* shares are read-only here: the game starts from disc 1 */
    FILE *f = fopen(path, "w");
    if (!f)
        return false;
    for (int i = 0; i < count; ++i)
        fprintf(f, "%s\n", discs[i]);
    return fclose(f) == 0;
}

/* One folder = one game. */
static void scan_game_folder(Library *lib, const char *dir, const char *folder_name)
{
    static NameBuf names[MAX_DIR_FILES];
    static NameBuf discs[MAX_DIR_FILES];
    int n = list_images(dir, names);
    if (n == 0)
        return;

    int best = -1, best_rank = 0;
    bool has_sheet = false;
    int disc_count = 0;
    for (int i = 0; i < n; ++i)
    {
        int rank = image_rank(names[i]);
        if (rank == 3)
            has_sheet = true;
        if (rank > best_rank)
        {
            best_rank = rank;
            best = i;
        }
    }
    /* Collect disc images of the winning kind (cue/chd/pbp...). */
    for (int i = 0; i < n; ++i)
        if (image_rank(names[i]) == best_rank && best_rank != 6)
            str_copy(discs[disc_count++], sizeof(NameBuf), names[i]);
    if (best_rank == 1 && has_sheet)
        return;

    Game *g = add_game(lib);
    if (!g)
        return;
    clean_title(folder_name, g->title, sizeof(g->title));
    extract_region(folder_name, g->serial, sizeof(g->serial));
    if (!g->serial[0])
        extract_region(names[best], g->serial, sizeof(g->serial));
    g->discs = 1;

    if (best_rank == 6)
    {
        path_join(g->path, sizeof(g->path), dir, names[best]);
        /* count lines for the disc indicator */
        VFile *f = vf_open(g->path);
        if (f)
        {
            char line[512];
            int lines = 0;
            while (vf_gets(line, sizeof(line), f))
                if (line[0] && line[0] != '#' && line[0] != '\n' && line[0] != '\r')
                    ++lines;
            vf_close(f);
            g->discs = lines > 0 ? lines : 1;
        }
    }
    else if (disc_count > 1 && (contains_icase(discs[0], "disc") || contains_icase(discs[0], "cd")))
    {
        char m3u[PSXS5_PATH_MAX];
        char file[160];
        snprintf(file, sizeof(file), "%.150s.m3u", g->title);
        path_join(m3u, sizeof(m3u), dir, file);
        if (write_m3u(m3u, discs, disc_count))
        {
            str_copy(g->path, sizeof(g->path), m3u);
            g->discs = disc_count;
            psxs5_log("library: wrote %s (%d discs)", m3u, disc_count);
        }
        else
            path_join(g->path, sizeof(g->path), dir, discs[0]);
    }
    else
        path_join(g->path, sizeof(g->path), dir, names[best]);

    str_copy(g->folder, sizeof(g->folder), dir);
    set_disc_name(g, disc_count > 0 ? discs[0] : names[best]);
    finish_game(g);
}

/* the folders among a root's entries (names sorted, as the entries are) */
#define MAX_ROOT_ENTRIES 32768
static NameBuf *root_dirs;
static int root_dir_count;

static bool is_root_dir(const char *name)
{
    return bsearch(name, root_dirs, (size_t)root_dir_count, sizeof(NameBuf), cmp_names) != NULL;
}

static void scan_root(Library *lib, const char *root)
{
    VDir *d = vd_open(root); /* a local folder or one on a network share */
    if (!d)
        return;
    /* as many entries as the folder has (a share's one big folder: thousands) */
    static NameBuf *entries;
    static bool *skip;
    static int room;
    int n = 0;
    root_dir_count = 0;
    const char *name;
    bool dir = false;
    while ((name = vd_next(d, &dir)) && n < MAX_ROOT_ENTRIES)
    {
        if (name[0] == '.')
            continue;
        if (n == room)
        {
            int more = room ? room * 2 : 1024;
            NameBuf *e = realloc(entries, sizeof(NameBuf) * (size_t)more);
            NameBuf *r = e ? realloc(root_dirs, sizeof(NameBuf) * (size_t)more) : NULL;
            if (e)
                entries = e;
            if (r)
                root_dirs = r;
            bool *s = r ? realloc(skip, sizeof(bool) * (size_t)more) : NULL;
            if (!s)
                break;
            skip = s;
            room = more;
        }
        str_copy(entries[n++], sizeof(NameBuf), name);
        if (dir)
            str_copy(root_dirs[root_dir_count++], sizeof(NameBuf), name);
    }
    vd_close(d);
    if (n == 0)
        return;
    qsort(entries, (size_t)n, sizeof(NameBuf), cmp_names);
    qsort(root_dirs, (size_t)root_dir_count, sizeof(NameBuf), cmp_names);

    bool root_has_sheet = false;
    for (int i = 0; i < n; ++i)
        if (image_rank(entries[i]) == 3)
            root_has_sheet = true;

    /* Multi-disc games lying loose here ("FF VII (Disc 1).cue", "(Disc 2).cue"...)
     * are one game, as in their own folder: the discs a playlist (.m3u) here
     * already lists are skipped, the others get one written, and the shelf
     * shows the game once, from disc 1. */
    memset(skip, 0, sizeof(bool) * (size_t)n);
    for (int i = 0; i < n; ++i)
        if (image_rank(entries[i]) == 6)
        {
            /* a playlist made elsewhere (VLC...) may hold a PC's paths: it counts
             * only when every disc it lists is here */
            char m3u[PSXS5_PATH_MAX], line[300];
            path_join(m3u, sizeof(m3u), root, entries[i]);
            VFile *f = vf_open(m3u);
            int listed[MAX_DIR_FILES], count = 0;
            bool valid = f != NULL;
            while (f && vf_gets(line, sizeof(line), f))
            {
                line[strcspn(line, "\r\n")] = '\0';
                if (!line[0] || line[0] == '#')
                    continue;
                int found = -1;
                for (int k = 0; k < n && found < 0; ++k)
                    if (!str_icmp(entries[k], line))
                        found = k;
                if (found < 0)
                    valid = false;
                else if (count < MAX_DIR_FILES)
                    listed[count++] = found;
            }
            if (f)
                vf_close(f);
            if (valid && count > 0)
                for (int k = 0; k < count; ++k)
                    skip[listed[k]] = true;
            else
            {
                skip[i] = true; /* not usable here: PSXS5 groups the discs itself */
                psxs5_log("library: %s lists discs that aren't beside it, ignored", m3u);
            }
        }
    for (int i = 0; i < n; ++i)
    {
        int rank = image_rank(entries[i]);
        if (skip[i] || rank < 2 || rank == 6 ||
            !(contains_icase(entries[i], "(disc") || contains_icase(entries[i], "(cd")))
            continue;
        char title[160], other[160];
        clean_title(entries[i], title, sizeof(title));
        static NameBuf discs[MAX_DIR_FILES];
        int count = 0, members[MAX_DIR_FILES];
        for (int k = i; k < n && count < MAX_DIR_FILES; ++k)
        {
            if (skip[k] || image_rank(entries[k]) != rank ||
                !(contains_icase(entries[k], "(disc") || contains_icase(entries[k], "(cd")))
                continue;
            clean_title(entries[k], other, sizeof(other));
            if (str_icmp(title, other) != 0)
                continue;
            members[count] = k;
            str_copy(discs[count++], sizeof(NameBuf), entries[k]);
        }
        if (count < 2)
            continue;
        for (int k = 0; k < count; ++k)
            skip[members[k]] = true;
        Game *g = add_game(lib);
        if (!g)
            return;
        str_copy(g->title, sizeof(g->title), title);
        extract_region(discs[0], g->serial, sizeof(g->serial));
        str_copy(g->folder, sizeof(g->folder), root);
        char m3u_name[200], m3u[PSXS5_PATH_MAX];
        snprintf(m3u_name, sizeof(m3u_name), "%.190s.m3u", title);
        path_join(m3u, sizeof(m3u), root, m3u_name);
        if (vf_exists(m3u))
        {
            /* that name is taken by a playlist that didn't work here: keep it */
            snprintf(m3u_name, sizeof(m3u_name), "%.180s (PSXS5).m3u", title);
            path_join(m3u, sizeof(m3u), root, m3u_name);
        }
        char first[PSXS5_PATH_MAX];
        path_join(first, sizeof(first), root, discs[0]);
        if (write_m3u(m3u, discs, count))
        {
            str_copy(g->path, sizeof(g->path), m3u);
            psxs5_log("library: wrote %s (%d discs)", m3u, count);
        }
        else
            str_copy(g->path, sizeof(g->path), first);
        g->discs = count;
        set_disc_name(g, discs[0]);
        finish_game(g);
    }

    for (int i = 0; i < n; ++i)
    {
        if (skip[i])
            continue;
        char full[PSXS5_PATH_MAX];
        path_join(full, sizeof(full), root, entries[i]);
        if (is_root_dir(entries[i]))
        {
            /* a PSXS5 folder copied whole to a USB drive: its bios/, saves/... aren't games
             * ("games" is scanned as a root of its own) */
            static const char *const not_games[] = {"bios",  "saves", "states", "cache", "logs",
                                                     "cheats", "art",  "covers", "games", "bezels",
                                                     "textures", "profiles", "translations", NULL};
            bool skip_dir = false;
            for (const char *const *s = not_games; *s; ++s)
                skip_dir |= str_icmp(entries[i], *s) == 0;
            if (!skip_dir)
                scan_game_folder(lib, full, entries[i]);
            continue;
        }
        int rank = image_rank(entries[i]);
        if (rank == 0 || (rank == 1 && root_has_sheet))
            continue;
        Game *g = add_game(lib);
        if (!g)
            return;
        clean_title(entries[i], g->title, sizeof(g->title));
        extract_region(entries[i], g->serial, sizeof(g->serial));
        str_copy(g->path, sizeof(g->path), full);
        str_copy(g->folder, sizeof(g->folder), root);
        set_disc_name(g, entries[i]);
        g->discs = 1;
        if (rank == 6)
        {
            /* a playlist: one disc per line */
            VFile *f = vf_open(full);
            char line[300];
            int lines = 0;
            while (f && vf_gets(line, sizeof(line), f))
                lines += line[0] && line[0] != '\n' && line[0] != '\r' && line[0] != '#';
            vf_close(f);
            g->discs = lines > 0 ? lines : 1;
        }
        finish_game(g);
    }
}

static int cmp_games(const void *a, const void *b)
{
    return str_icmp(((const Game *)a)->title, ((const Game *)b)->title);
}

void library_scan_into(Library *lib, const char *root)
{
    if (netfs_is_url(root) || path_is_dir(root))
        scan_root(lib, root);
}

void library_sort(Library *lib)
{
    if (lib->count > 1)
        qsort(lib->games, (size_t)lib->count, sizeof(Game), cmp_games);
}

bool library_save_index(const Library *lib, int from, int to, const char *index_path)
{
    char temp[PSXS5_PATH_MAX + 8];
    snprintf(temp, sizeof(temp), "%s.new", index_path);
    FILE *f = fopen(temp, "w");
    if (!f)
        return false;
    fprintf(f, "# Game Boy Advance's list of a network share's games: title, serial, discs, path, disc name\n");
    for (int i = from; i < to && i < lib->count; ++i)
    {
        const Game *g = &lib->games[i];
        fprintf(f, "%s\t%s\t%d\t%s\t%s\n", g->title, g->serial, g->discs, g->path, g->disc_name);
    }
    bool ok = fclose(f) == 0 && rename(temp, index_path) == 0;
    if (!ok)
        remove(temp);
    return ok;
}

void library_scan(Library *lib, const char *const *roots, int root_count)
{
    lib->count = 0;
    for (int i = 0; i < root_count; ++i)
        if (netfs_is_url(roots[i]) || path_is_dir(roots[i]))
            scan_root(lib, roots[i]);
    if (lib->count > 1)
        qsort(lib->games, (size_t)lib->count, sizeof(Game), cmp_games);
    psxs5_log("library: %d games", lib->count);
}

static bool load_index(Library *lib, const char *index_path, bool append);

bool library_load_index(Library *lib, const char *index_path)
{
    return load_index(lib, index_path, false);
}

bool library_append_index(Library *lib, const char *index_path)
{
    return load_index(lib, index_path, true);
}

static bool load_index(Library *lib, const char *index_path, bool append)
{
    FILE *f = fopen(index_path, "r");
    if (!f)
        return false;
    int first = lib->count;
    if (!append)
        lib->count = first = 0;
    char line[1024];
    while (fgets(line, sizeof(line), f))
    {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0] || line[0] == '#')
            continue;
        char *field[5] = {0};
        char *p = line;
        for (int i = 0; i < 5 && p; ++i)
        {
            field[i] = p;
            p = strchr(p, '\t');
            if (p)
                *p++ = '\0';
        }
        if (!field[3] || !field[3][0])
            continue;
        Game *g = add_game(lib);
        if (!g)
            break;
        clean_title(field[0][0] ? field[0] : "Game", g->title, sizeof(g->title));
        str_copy(g->serial, sizeof(g->serial), field[1] ? field[1] : "");
        g->discs = field[2] ? atoi(field[2]) : 1;
        if (g->discs < 1)
            g->discs = 1;
        str_copy(g->path, sizeof(g->path), field[3]);
        str_copy(g->folder, sizeof(g->folder), field[3]);
        char *slash = strrchr(g->folder, '/');
        if (slash)
            *slash = '\0';
        /* already without extension; don't cut names like "Dr. Mario" at the dot */
        str_copy(g->disc_name, sizeof(g->disc_name), field[4] && field[4][0] ? field[4] : g->title);
        if (g->serial[0])
            str_copy(g->id, sizeof(g->id), g->serial);
        else
            finish_game(g); /* derive an id from the title */
    }
    fclose(f);
    if (!append && lib->count > 1)
        qsort(lib->games, (size_t)lib->count, sizeof(Game), cmp_games);
    psxs5_log("library: %d games from index %s", lib->count - first, index_path);
    return true;
}

void library_free(Library *lib)
{
    free(lib->games);
    memset(lib, 0, sizeof(*lib));
}
