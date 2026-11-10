#!/usr/bin/env python3
# GBAR5 - the project page and the tip jar as QR codes for Settings > About.
# SPDX-License-Identifier: GPL-3.0-or-later
"""python -m pip install segno pillow; python tools/make_qr.py

Writes assets/qr-github.png (the project page) and assets/qr-kofi.png (the
tip jar). One pixel per module with a 2-module white margin: the app scales
them up crisply on the console."""
from pathlib import Path

import segno
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
CODES = {
    "qr-github.png": "https://github.com/thereal47Ronin/GBAR5",
    "qr-kofi.png": "https://ko-fi.com/thereal47ronin",
}

for name, url in CODES.items():
    matrix = segno.make(url, error="m").matrix
    n, quiet = len(matrix), 2
    img = Image.new("RGBA", (n + 2 * quiet, n + 2 * quiet), (255, 255, 255, 255))
    for y, row in enumerate(matrix):
        for x, dark in enumerate(row):
            if dark:
                img.putpixel((x + quiet, y + quiet), (15, 19, 48, 255))
    out = ROOT / "assets" / name
    img.save(out, optimize=True)
    print("wrote", out, "for", url)
