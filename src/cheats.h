/*
 * GBAR5 - RetroArch .cht cheat files (CodeBreaker / GameShark codes).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PSXS5_CHEATS_H
#define PSXS5_CHEATS_H

#include "library.h"

#define CHEATS_MAX 512

typedef struct
{
    char desc[80];
    char code[256]; /* "3200E924 0096+330034B8 0096" */
    bool enabled;
    bool patch;     /* a patch (a fix, not a cheat): listed first */
} Cheat;

typedef struct
{
    Cheat items[CHEATS_MAX];
    int count;
    char source[PSXS5_PATH_MAX]; /* the .cht that was loaded, "" if none */
    char state_path[PSXS5_PATH_MAX];
} CheatList;

/* Finds the best .cht for `game`: one beside the game files first, then what
 * is in `cheats_dir`, then the game's own file in libretro-database's Game Boy
 * Advance set, which is downloaded while the game starts. Restores which codes
 * were switched on last time. */
bool cheats_load(CheatList *list, const Game *game, const char *cheats_dir);
void cheats_clear(CheatList *list);
/* True once when a cheat file cheats_load started downloading has arrived:
 * load again then. */
bool cheats_fetch_finished(void);
/* A download is on its way (the Cheats screen says so). */
bool cheats_fetch_running(void);
/* A code of the player's own (Find a code): saved to cheats/mine/<id>.txt,
 * added to the list and switched on. */
bool cheats_add_user(CheatList *list, const Game *game, const char *cheats_dir, const char *desc,
                     const char *code);
/* Two titles with the same words ("Disney-Pixar's ..." = "Disney-Pixar ...",
 * "VII" = "7", "The Legend of X" = "Legend of X, The"). */
bool titles_match(const char *a, const char *b);
/* The best match for the game in a list of No-Intro-style names (an assets/
 * file, one per line, as cheats-index.txt); false when nothing matches. */
bool cheats_best_in_index(const Game *game, const char *asset, char *name, size_t size);

/* Pushes enabled codes into the running core and remembers the selection. */
void cheats_apply(const CheatList *list);
void cheats_save_selection(const CheatList *list);

#endif
