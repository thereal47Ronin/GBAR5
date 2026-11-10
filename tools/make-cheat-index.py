#!/usr/bin/env python3
"""GBAR5 - writes assets/cheats-index.txt: the names of libretro-database's
Game Boy Advance cheat files (cht/Nintendo - Game Boy Advance). GBAR5 matches a
game against this list and downloads just that one file, so a game gets its
cheats the moment it is started - no library on the console, no folder listing
(which a sandboxed app can't do anyway).

    python tools/make-cheat-index.py        (rerun now and then for new files)

SPDX-License-Identifier: GPL-3.0-or-later
"""
import json
import pathlib
import urllib.parse
import urllib.request

REPO = "libretro/libretro-database"
FOLDER = "cht/Nintendo - Game Boy Advance/"


def get(url):
    req = urllib.request.Request(url, headers={"User-Agent": "GBAR5", "Accept": "application/vnd.github+json"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.load(r)


def main():
    branch = get(f"https://api.github.com/repos/{REPO}")["default_branch"]
    # the folder alone: the repository's whole listing is huge and comes back
    # truncated once it grows
    tree_ref = urllib.parse.quote(f"{branch}:{FOLDER.rstrip('/')}", safe="")
    tree = get(f"https://api.github.com/repos/{REPO}/git/trees/{tree_ref}")
    if tree.get("truncated"):
        raise SystemExit("the folder listing was truncated")
    names = sorted(e["path"] for e in tree["tree"] if e["type"] == "blob" and e["path"].endswith(".cht"))
    out = pathlib.Path(__file__).resolve().parent.parent / "assets" / "cheats-index.txt"
    out.write_text(f"# {REPO} {branch}: {FOLDER} ({len(names)} files)\n" + "\n".join(names) + "\n",
                   encoding="utf-8", newline="\n")
    print(f"{len(names)} cheat files -> {out}")


if __name__ == "__main__":
    main()
