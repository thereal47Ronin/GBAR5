/*
 * PSXS5 - game library scanning.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PSXS5_LIBRARY_H
#define PSXS5_LIBRARY_H

#include "psxs5.h"

typedef struct
{
    char title[96];
    char serial[16];               /* e.g. SLUS-00421 when the name carries it */
    char code[8];                  /* GBA cartridge header game code, e.g. AGSE */
    char rom_title[16];            /* the twelve characters in the GBA header */
    char path[PSXS5_PATH_MAX];     /* what the core loads: .m3u/.cue/.chd/.pbp/... */
    char id[160];                  /* stable key for save states (a GBA file name) */
    char disc_name[160];           /* first disc file name without extension, for .cht lookup */
    char folder[PSXS5_PATH_MAX];   /* directory holding the game files */
    int discs;
} Game;

typedef struct
{
    Game *games;
    int count;
    int capacity;
} Library;

/* Scans every existing root. Each root holds one game per folder, or loose
 * images. Multi-disc folders without an .m3u get one written for them. */
void library_scan(Library *lib, const char *const *roots, int root_count);
/* one root more, without sorting (library_sort after) */
void library_scan_into(Library *lib, const char *root);
void library_sort(Library *lib);
/* games from..to as an index file, and an index's games added to the library */
bool library_save_index(const Library *lib, int from, int to, const char *index_path);
bool library_append_index(Library *lib, const char *index_path);

/* For a sandboxed PSXS5 (folders can't be listed): reads the index written by
 * tools/psxs5_sync.py, one game per line:
 *   title <TAB> serial <TAB> discs <TAB> /data/GBAR5/games/.../file.m3u <TAB> disc name
 * Returns false when the index can't be opened. */
bool library_load_index(Library *lib, const char *index_path);
void library_free(Library *lib);

#endif
