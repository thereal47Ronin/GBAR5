#!/usr/bin/env python3
"""GBAR5 - writes the lists of downloadable Game Boy Advance art GBAR5
matches a game against (as it does for cheats), so it downloads just that one
picture:
  assets/snaps-index.txt    libretro-thumbnails: in-game screenshots (Named_Snaps)
  assets/titles-index.txt   libretro-thumbnails: title screens (Named_Titles)
  assets/boxarts-index.txt  libretro-thumbnails: box art by name (Named_Boxarts)

The shelf's background picture and the picture in the cartridge screen come
from Named_Snaps: the file names are No-Intro names, the same key the library
uses for a GBA game (its ROM file name).

    python tools/make-art-index.py        (rerun now and then for new art)

SPDX-License-Identifier: GPL-3.0-or-later
"""
import json
import pathlib
import urllib.parse
import urllib.request

REPO = "libretro-thumbnails/Nintendo_-_Game_Boy_Advance"
LISTS = [
    ("Named_Snaps/", "snaps-index.txt"),
    ("Named_Titles/", "titles-index.txt"),
    ("Named_Boxarts/", "boxarts-index.txt"),
]


def get(url):
    req = urllib.request.Request(url, headers={"User-Agent": "GBAR5", "Accept": "application/vnd.github+json"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.load(r)


def write(folder, file):
    branch = get(f"https://api.github.com/repos/{REPO}")["default_branch"]
    # the folder alone (the whole repository's listing is too long to come back whole)
    tree_ref = urllib.parse.quote(f"{branch}:{folder.rstrip('/')}", safe="")
    tree = get(f"https://api.github.com/repos/{REPO}/git/trees/{tree_ref}")
    if tree.get("truncated"):
        raise SystemExit("the folder listing was truncated")
    names = sorted(e["path"] for e in tree["tree"] if e["type"] == "blob" and e["path"].endswith(".png"))
    out = pathlib.Path(__file__).resolve().parent.parent / "assets" / file
    out.write_text(f"# {REPO} {branch}: {folder} ({len(names)} files)\n" + "\n".join(names) + "\n",
                   encoding="utf-8", newline="\n")
    print(f"{len(names)} pictures -> {out}")


def main():
    for folder, file in LISTS:
        write(folder, file)


if __name__ == "__main__":
    main()
