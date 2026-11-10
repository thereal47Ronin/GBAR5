/*
 * PSXS5 - interface languages.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PSXS5_I18N_H
#define PSXS5_I18N_H

enum Lang
{
    LANG_EN,
    LANG_FR,
    LANG_DE,
    LANG_ES,
    LANG_IT,
    LANG_PT, /* Portugal */
    LANG_ZH, /* Simplified Chinese */
    LANG_KO,
    LANG_JA,
    LANG_COUNT
};

/* The order before this version had French, Portuguese, Spanish, Japanese;
 * config files written then are remapped when they are read. */
enum LangOld
{
    LANG_OLD_EN = 0,
    LANG_OLD_FR = 1,
    LANG_OLD_PT = 2,
    LANG_OLD_ES = 3,
    LANG_OLD_JA = 4
};

void i18n_set(int lang);
int i18n_get(void);
/* The text in the current language; the English text itself when there is no
 * translation. Formats keep their printf conversions in the same order. */
const char *tr(const char *english) __attribute__((format_arg(1)));
/* Each language's own name, for the selector ("Français", "日本語"...). */
const char *i18n_name(int lang);
/* Every string of a language, for the font atlas (NULL-terminated walk). */
const char *i18n_string(int lang, int index);

#endif
