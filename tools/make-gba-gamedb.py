#!/usr/bin/env python3
"""GBAR5 - build assets/gba-gamedb.txt, what the shelf knows about a cartridge.

The shelf shows a Game Boy Advance game's release date, its genres, who made it
and how many can play at once. None of that is in a cartridge: the header only
carries a title, a four-letter game code and a maker code. So it is kept here,
one line per game, and read at start-up by src/gbadb.c.

The list is built from the LaunchBox Games Database (Metadata.xml, the platform
"Nintendo Game Boy Advance"), which is a free download and is not part of this
source tree:

    python3 tools/make-gba-gamedb.py --download     # fetches, then builds

or from a copy already on disk:

    python3 tools/make-gba-gamedb.py ~/Metadata.zip
    python3 tools/make-gba-gamedb.py ~/Metadata.xml

Line format (tab separated, UTF-8):

    name <TAB> released <TAB> genres <TAB> developer <TAB> publisher <TAB> players <TAB> kind

released is YYYY-MM-DD, or YYYY when only the year is known. genres are joined
with a middle dot. players is the most that can play at once (0 unknown). kind
is R for a released game, M for a ROM hack, H for homebrew, U for unlicensed,
X for anything else - the shelf prefers an R when two names look alike.

SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import io
import os
import re
import sys
import unicodedata
import zipfile
import xml.etree.ElementTree as ET

PLATFORM = "Nintendo Game Boy Advance"
URL = "https://gamesdb.launchbox-app.com/Metadata.zip"
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "assets", "gba-gamedb.txt")
CACHE = os.path.join(os.path.expanduser("~"), ".cache", "gbar5", "Metadata.zip")

KINDS = {
    "released": "R",
    "rom hack": "M",
    "homebrew": "H",
    "unlicensed": "U",
    "unreleased": "X",
    "cancelled": "X",
    "canceled": "X",
}


def fold(text):
    """The name cut down to letters and digits: 'Pokémon Emerald (U)' -> 'pokemonemerald u'."""
    text = unicodedata.normalize("NFKD", text)
    text = "".join(c for c in text if not unicodedata.combining(c))
    text = re.sub(r"[^0-9A-Za-z]+", " ", text).strip().lower()
    return re.sub(r"\s+", " ", text)


def clean(text, limit):
    """One field of one line: no tabs, no newlines, no runs of spaces."""
    text = re.sub(r"[\t\r\n]+", " ", (text or "").replace("\u00a0", " ")).strip()
    text = re.sub(r"\s+", " ", text)
    return text[:limit]


def date_of(game):
    date = clean(game.findtext("ReleaseDate") or "", 10)
    if re.match(r"^\d{4}-\d{2}-\d{2}", date):
        return date[:10]
    year = clean(game.findtext("ReleaseYear") or "", 4)
    if re.match(r"^\d{4}$", year):
        return year
    return ""


def genres_of(game):
    seen, out = set(), []
    for part in re.split(r"[;,]", game.findtext("Genres") or ""):
        part = clean(part, 40)
        if part and part.lower() not in seen:
            seen.add(part.lower())
            out.append(part)
    return " \u00b7 ".join(out[:3])


def players_of(game):
    raw = clean(game.findtext("MaxPlayers") or "", 3)
    try:
        n = int(raw)
    except ValueError:
        return 0
    return n if 0 <= n <= 4 else 0


def open_metadata(path):
    if path.lower().endswith(".zip"):
        archive = zipfile.ZipFile(path)
        name = next((n for n in archive.namelist() if n.lower().endswith("metadata.xml")), None)
        if not name:
            sys.exit("no Metadata.xml inside %s" % path)
        return archive.open(name)
    return open(path, "rb")


def download(target):
    import urllib.request

    os.makedirs(os.path.dirname(target), exist_ok=True)
    print("downloading %s" % URL)
    with urllib.request.urlopen(URL, timeout=300) as response, open(target + ".part", "wb") as out:
        shutil_copy(response, out)
    os.replace(target + ".part", target)
    print("  %s (%.1f MB)" % (target, os.path.getsize(target) / 1e6))


def shutil_copy(source, out):
    while True:
        chunk = source.read(1 << 20)
        if not chunk:
            break
        out.write(chunk)


def build(source, out_path):
    games = {}
    total = 0
    for _, element in ET.iterparse(source, events=("end",)):
        if element.tag != "Game":
            continue
        if clean(element.findtext("Platform") or "", 60) != PLATFORM:
            element.clear()
            continue
        total += 1
        name = clean(element.findtext("Name") or "", 120)
        if name:
            kind = KINDS.get(clean(element.findtext("ReleaseType") or "", 20).lower(), "X")
            record = (
                name,
                date_of(element),
                genres_of(element),
                clean(element.findtext("Developer") or "", 48),
                clean(element.findtext("Publisher") or "", 48),
                players_of(element),
                kind,
            )
            key = fold(name)
            # the same game twice: the released one, and the one with more to say, win
            old = games.get(key)
            if old is None or (old[6] != "R" and record[6] == "R") or (old[1] == "" and record[1]):
                games[key] = record
        element.clear()

    rows = sorted(games.values(), key=lambda r: fold(r[0]))
    with open(out_path, "w", encoding="utf-8", newline="\n") as out:
        out.write("# GBAR5 - what the shelf knows about a Game Boy Advance cartridge.\n")
        out.write("# Built by tools/make-gba-gamedb.py from the LaunchBox Games Database\n")
        out.write('# ("%s"); not part of the source. Do not edit: rebuild it,\n' % PLATFORM)
        out.write("# or put your own lines in <data>/gba-metadata.txt, which wins.\n")
        out.write("#\n")
        out.write("# name\treleased\tgenres\tdeveloper\tpublisher\tplayers\tkind\n")
        for row in rows:
            out.write(
                "%s\t%s\t%s\t%s\t%s\t%d\t%s\n"
                % (row[0], row[1], row[2], row[3], row[4], row[5], row[6])
            )

    dated = sum(1 for r in rows if r[1])
    described = sum(1 for r in rows if r[2])
    print(
        "%d cartridges in the database (%d with a release date, %d with genres) -> %s"
        % (len(rows), dated, described, out_path)
    )
    print("%d %s entries read, %.1f KB written" % (total, PLATFORM, os.path.getsize(out_path) / 1024))


def main():
    parser = argparse.ArgumentParser(description="Build assets/gba-gamedb.txt for GBAR5.")
    parser.add_argument("source", nargs="?", help="a LaunchBox Metadata.xml or Metadata.zip")
    parser.add_argument("--download", action="store_true", help="fetch %s first" % URL)
    parser.add_argument("--out", default=OUT, help="where to write the index (default: assets/gba-gamedb.txt)")
    args = parser.parse_args()

    source = args.source
    if args.download or not source:
        if not os.path.exists(CACHE) or args.download:
            download(CACHE)
        source = CACHE

    build(open_metadata(source), args.out)


if __name__ == "__main__":
    main()
