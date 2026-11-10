#!/usr/bin/env python3
# GBAR5 - builds the Japanese, Chinese and Korean interface font subsets.
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Noto Sans JP/SC/KR are 9-17 MB each; GBAR5 only needs the characters its own
translations use. This writes three small subsets into assets/fonts:

  NotoSansJP-PSXS5.ttf   (kana + the kanji used by the Japanese strings)
  NotoSansSC-PSXS5.ttf   (the hanzi used by the Simplified Chinese strings)
  NotoSansKR-PSXS5.ttf   (the hangul used by the Korean strings)

Each one also carries every character of the language selector's names, so the
selector always draws its own labels. SIL Open Font License 1.1.

  pip install fonttools
  python tools/make_lang_fonts.py     (run again after adding translations)
"""
import io
import re
import urllib.request
from pathlib import Path

from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

ROOT = Path(__file__).resolve().parent.parent
CACHE = ROOT / "build"
OUT = ROOT / "assets" / "fonts"

SOURCES = {
    "JP": "https://github.com/google/fonts/raw/main/ofl/notosansjp/NotoSansJP%5Bwght%5D.ttf",
    "SC": "https://github.com/google/fonts/raw/main/ofl/notosanssc/NotoSansSC%5Bwght%5D.ttf",
    "KR": "https://github.com/google/fonts/raw/main/ofl/notosanskr/NotoSansKR%5Bwght%5D.ttf",
}

STRING = re.compile(r'"((?:[^"\\]|\\.)*)"', re.S)
LANGS = ["en", "fr", "de", "es", "it", "pt", "zh", "ko", "ja"]


def decode(body: str) -> str:
    out = bytearray()
    i = 0
    while i < len(body):
        if body[i] == "\\" and i + 1 < len(body):
            n = body[i + 1]
            if n == "n":
                out += b"\n"; i += 2
            elif n == "t":
                out += b"\t"; i += 2
            elif n == "x":
                try:
                    out.append(int(body[i + 2:i + 4], 16)); i += 4
                    continue
                except ValueError:
                    pass
                out += n.encode(); i += 2
            else:
                out += n.encode(); i += 2
        else:
            out += body[i].encode("utf-8"); i += 1
    return out.decode("utf-8", "replace")


def table_columns():
    """Every language's strings, as a list of columns."""
    text = (ROOT / "src" / "i18n.c").read_text(encoding="utf-8")
    start = text.index("static const Entry ENTRIES[] = {")
    start = text.index("{", start + len("static const Entry ENTRIES[] ="))
    depth = 0
    i = start
    while i < len(text):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                break
        i += 1
    body = text[start + 1:i]
    items, cur, depth = [], "", 0
    for ch in body:
        if ch == "{":
            depth += 1
            if depth == 1:
                cur = ""
                continue
        elif ch == "}":
            depth -= 1
            if depth == 0:
                items.append(cur)
                continue
        if depth >= 1:
            cur += ch
    columns = {lang: [] for lang in LANGS}
    for item in items:
        parts, cur, in_str, esc = [], "", False, False
        for ch in item:
            if in_str:
                cur += ch
                if esc:
                    esc = False
                elif ch == "\\":
                    esc = True
                elif ch == '"':
                    in_str = False
            else:
                if ch == '"':
                    in_str = True
                    cur += ch
                elif ch == ",":
                    parts.append(cur)
                    cur = ""
                else:
                    cur += ch
        if cur.strip():
            parts.append(cur)
        for idx, p in enumerate(parts[: len(LANGS)]):
            lit = "".join(STRING.findall(p))
            if lit:
                columns[LANGS[idx]].append(decode(lit))
    return columns


def wanted(columns, lang: str, extra_ranges=()) -> set[int]:
    cps = {ord(c) for s in columns[lang] for c in s if ord(c) > 0xFF}
    cps |= {ord(c) for s in columns["en"] for c in s if ord(c) > 0xFF}  # selector names
    cps |= set(range(0x3000, 0x3040))  # CJK punctuation
    cps |= set(range(0xFF01, 0xFF5F))  # full-width forms
    cps |= {0x30FB, 0x30FC}
    for r in extra_ranges:
        cps |= set(r)
    return cps


def build(tag: str, cps: set[int]) -> None:
    cache = CACHE / f"NotoSans{tag}-wght.ttf"
    if not cache.exists():
        CACHE.mkdir(parents=True, exist_ok=True)
        print(f"downloading Noto Sans {tag}...")
        with urllib.request.urlopen(SOURCES[tag], timeout=300) as r:
            cache.write_bytes(r.read())
    font = TTFont(cache)
    font = instancer.instantiateVariableFont(font, {"wght": 500})
    options = subset.Options()
    options.layout_features = ["*"]
    options.name_IDs = ["*"]
    options.notdef_outline = True
    options.hinting = False
    sub = subset.Subsetter(options)
    sub.populate(unicodes=cps)
    sub.subset(font)
    buf = io.BytesIO()
    font.save(buf)
    out = OUT / f"NotoSans{tag}-PSXS5.ttf"
    out.write_bytes(buf.getvalue())
    print(f"wrote {out} ({out.stat().st_size // 1024} KB, {len(cps)} characters)")


def main() -> None:
    columns = table_columns()
    build("JP", wanted(columns, "ja", (range(0x3040, 0x3100),)))  # + hiragana/katakana
    build("SC", wanted(columns, "zh"))
    build("KR", wanted(columns, "ko"))


if __name__ == "__main__":
    main()
