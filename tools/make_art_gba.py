#!/usr/bin/env python3
# GBAR5 - home-screen art: icon0.png (512x512), the 3840x2160 selection
# background and the 3840x2160 launch background.
# SPDX-License-Identifier: GPL-3.0-or-later
"""Draws GBAR5's own artwork, from scratch, in emerald.

Nothing here is borrowed: no console is reproduced, no logo, no wordmark that
belongs to anyone else. The mark is an original cartridge shape with a 3:2
screen window (the GBA's picture shape) and a pixel sunset drawn at 1x.

    python tools/make_art_gba.py    -> sce_sys/icon0.png
                                       sce_sys/background-source.png
                                       sce_sys/launch-background-source.png
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parent.parent
FONT_BOLD = str(ROOT / "assets/fonts/ChakraPetch-700.ttf")
FONT_REGULAR = str(ROOT / "assets/fonts/ChakraPetch-500.ttf")

# the emerald palette, the same greens the interface uses
BG_DEEP = (5, 14, 10)
BG = (8, 24, 16)
BG_MID = (12, 40, 27)
SHELL_TOP = (26, 74, 51)
SHELL_BOTTOM = (14, 46, 32)
SHELL_EDGE = (8, 26, 18)
LABEL = (7, 20, 13)
EMERALD = (63, 208, 138)
MINT = (231, 245, 237)
MUTED = (127, 165, 147)
AMBER = (227, 195, 106)


def vgradient(w, h, top, bottom):
    img = Image.new("RGB", (w, h), top)
    d = ImageDraw.Draw(img)
    for y in range(h):
        k = y / max(1, h - 1)
        d.line([(0, y), (w, y)],
               fill=tuple(int(top[i] + (bottom[i] - top[i]) * k) for i in range(3)))
    return img


def pixel_sunset(w, h):
    """A tiny 3:2 screen: sky, sun, hills, drawn at 3x then squared off."""
    K = 3
    W, H = w * K, h * K
    img = Image.new("RGB", (W, H), (24, 74, 58))
    d = ImageDraw.Draw(img)
    for y in range(H // 2):
        k = y / max(1, H // 2 - 1)
        d.line([(0, y), (W, y)], fill=(int(20 + 40 * k), int(70 + 50 * k), int(56 + 30 * k)))
    r = max(2, H // 7)
    sx, sy = int(W * 0.70), int(H * 0.30)
    d.ellipse((sx - r, sy - r, sx + r, sy + r), fill=AMBER)
    d.polygon([(0, int(H * 0.66)), (int(W * 0.30), int(H * 0.46)), (int(W * 0.62), int(H * 0.70)),
               (int(W * 0.80), int(H * 0.56)), (W, int(H * 0.72)), (W, H), (0, H)], fill=(20, 96, 68))
    d.polygon([(0, int(H * 0.80)), (int(W * 0.38), int(H * 0.68)), (int(W * 0.74), int(H * 0.84)),
               (W, int(H * 0.76)), (W, H), (0, H)], fill=(12, 60, 44))
    # a little runner, standing on the near hill
    sz = max(3, H // 20)
    px, py = int(W * 0.32), int(H * 0.74)
    d.rectangle((px, py - 4 * sz, px + 2 * sz, py - 2 * sz), fill=MINT)          # head
    d.rectangle((px, py - 2 * sz, px + 2 * sz, py - sz), fill=MINT)               # body
    d.rectangle((px, py - sz, px + sz - 1, py), fill=MINT)                        # legs
    d.rectangle((px + sz + 1, py - sz, px + 2 * sz, py), fill=MINT)
    return img.resize((w, h), Image.NEAREST)


def cartridge(width):
    """An original cartridge mark: a rounded shell, a 3:2 label window, a grip.
    `width` is the full width in pixels. Straight on, transparent background."""
    S = 4
    W, H = int(width * S), int(width * 1.16 * S)
    layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))

    m = int(W * 0.05)
    bw, bh = W - 2 * m, H - 2 * m
    x0, y0, x1, y1 = m, m, m + bw, m + bh
    radius = int(bw * 0.10)

    # the shadow under it
    sh = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(sh).rounded_rectangle((x0 + bw * 0.02, y0 + bh * 0.03, x1, y1),
                                         radius=radius, fill=(0, 0, 0, 120))
    layer = Image.alpha_composite(layer, sh.filter(ImageFilter.GaussianBlur(bw * 0.035)))
    d = ImageDraw.Draw(layer)

    # the shell
    body = vgradient(bw, bh, SHELL_TOP, SHELL_BOTTOM).convert("RGBA")
    mask = Image.new("L", (bw, bh), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, bw - 1, bh - 1), radius=radius, fill=255)
    layer.paste(body, (x0, y0), mask)
    d.rounded_rectangle((x0, y0, x1, y1), radius=radius, outline=SHELL_EDGE + (200,), width=max(2, S * 2))

    # a soft highlight down the left edge
    hi = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(hi).rounded_rectangle((x0 + bw * 0.02, y0 + bh * 0.03, x0 + bw * 0.12, y1 - bh * 0.06),
                                         radius=int(bw * 0.04), fill=(255, 255, 255, 22))
    layer = Image.alpha_composite(layer, hi.filter(ImageFilter.GaussianBlur(bw * 0.01)))
    d = ImageDraw.Draw(layer)

    # the label window: 3:2, the GBA's own picture shape
    lw = int(bw * 0.74)
    lh = int(lw * 2 / 3)
    lx = x0 + (bw - lw) // 2
    ly = y0 + int(bh * 0.10)
    d.rounded_rectangle((lx - int(bw * 0.035), ly - int(bw * 0.035),
                        lx + lw + int(bw * 0.035), ly + lh + int(bw * 0.035)),
                        radius=int(bw * 0.05), fill=LABEL + (255,))
    layer.paste(pixel_sunset(lw, lh).convert("RGB"), (lx, ly))
    d.rounded_rectangle((lx - int(bw * 0.035), ly - int(bw * 0.035),
                        lx + lw + int(bw * 0.035), ly + lh + int(bw * 0.035)),
                        radius=int(bw * 0.05), outline=(255, 255, 255, 46), width=max(1, S))

    # the power light and the grip, drawn as abstract parts, not a replica
    led = max(2, int(bw * 0.022))
    d.ellipse((lx + lw - led * 2, ly + lh + int(bw * 0.055) - led,
               lx + lw, ly + lh + int(bw * 0.055) + led), fill=EMERALD + (255,))
    gy = y0 + int(bh * 0.82)
    d.rounded_rectangle((x0 + bw * 0.10, gy, x1 - bw * 0.10, gy + bh * 0.10),
                        radius=int(bw * 0.04), fill=SHELL_EDGE + (255,))
    for i in range(3):
        yy = gy + int(bh * 0.018) + i * int(bh * 0.024)
        d.rectangle((x0 + bw * 0.20, yy, x1 - bw * 0.20, yy + max(1, S)),
                    fill=(255, 255, 255, 34))

    return layer.resize((width, int(width * 1.16)), Image.LANCZOS)


def spaced_text(d, xy, text, font, fill, tracking):
    """Draws text with a little tracking; returns the width used."""
    x, y = xy
    for ch in text:
        d.text((x, y), ch, font=font, fill=fill)
        x += d.textlength(ch, font=font) + tracking
    return x - xy[0] - tracking


def make_icon(path, size=512):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))

    tile = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    body = vgradient(size, size, BG_MID, BG_DEEP).convert("RGBA")
    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).rounded_rectangle((10, 10, size - 10, size - 10), radius=int(size * 0.21), fill=255)
    tile.paste(body, (0, 0), mask)
    glow = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    ImageDraw.Draw(glow).ellipse((size * 0.10, size * 0.02, size * 0.90, size * 0.70),
                                 fill=EMERALD + (70,))
    tile = Image.alpha_composite(tile, glow.filter(ImageFilter.GaussianBlur(size * 0.11)))
    d = ImageDraw.Draw(tile)
    d.rounded_rectangle((10, 10, size - 10, size - 10), radius=int(size * 0.21),
                        outline=EMERALD + (90,), width=max(2, size // 128))
    img = Image.alpha_composite(img, tile)

    mark = cartridge(int(size * 0.56))
    img.alpha_composite(mark, ((size - mark.width) // 2, int(size * 0.10)))

    d = ImageDraw.Draw(img)
    f = ImageFont.truetype(FONT_BOLD, int(size * 0.135))
    label = "GBAR5"
    tw = d.textlength(label, font=f)
    d.text(((size - tw) / 2, size * 0.745), label, font=f, fill=MINT + (255,))
    f2 = ImageFont.truetype(FONT_REGULAR, int(size * 0.045))
    sub = "EMERALD"
    tw2 = d.textlength(sub, font=f2)
    d.text(((size - tw2) / 2, size * 0.895), sub, font=f2, fill=MUTED + (230,))

    img.convert("RGB").save(path, "PNG", optimize=True)
    print("wrote", path, img.size)


def arena(size):
    """The common ground: deep emerald, a dot matrix, a soft glow, a vignette."""
    w, h = size
    img = vgradient(w, h, BG_MID, BG_DEEP).convert("RGBA")
    # a wide pool of light behind the mark
    glow = Image.new("RGBA", size, (0, 0, 0, 0))
    ImageDraw.Draw(glow).ellipse((w * 0.42, -h * 0.10, w * 1.10, h * 1.10), fill=EMERALD + (48,))
    img = Image.alpha_composite(img, glow.filter(ImageFilter.GaussianBlur(w * 0.055)))
    d = ImageDraw.Draw(img)
    # the GBA's dot matrix, faint
    step = max(4, w // 640)
    dot = (24, 68, 48, 90)
    for y in range(0, h, step):
        for x in range(0, w, step):
            d.point((x, y), fill=dot)
    # vignette
    vig = Image.new("RGBA", size, (0, 0, 0, 0))
    vd = ImageDraw.Draw(vig)
    vd.ellipse((-w * 0.15, -h * 0.20, w * 1.15, h * 1.20), fill=(0, 0, 0, 0))
    vd.rectangle((0, 0, w, h), outline=(0, 0, 0, 150), width=int(h * 0.16))
    img = Image.alpha_composite(img, vig.filter(ImageFilter.GaussianBlur(w * 0.05)))
    return img


def make_background(path, size=(3840, 2160)):
    w, h = size
    img = arena(size)

    mark = cartridge(int(h * 0.62))
    img.alpha_composite(mark, (int(w * 0.56), int(h * 0.17)))

    d = ImageDraw.Draw(img)
    f1 = ImageFont.truetype(FONT_BOLD, int(h * 0.145))
    spaced_text(d, (w * 0.075, h * 0.34), "GBAR5", f1, MINT + (255,), int(h * 0.006))
    f2 = ImageFont.truetype(FONT_REGULAR, int(h * 0.040))
    d.text((w * 0.080, h * 0.545), "your cartridges, your backups", font=f2, fill=MUTED + (240,))
    f3 = ImageFont.truetype(FONT_REGULAR, int(h * 0.030))
    d.text((w * 0.080, h * 0.610), "emerald shelf", font=f3, fill=EMERALD + (220,))
    # the same thin rule the interface uses
    d.rectangle((w * 0.080, h * 0.505, w * 0.080 + w * 0.16, h * 0.505 + max(2, h // 540)),
                fill=EMERALD + (200,))

    img.convert("RGB").save(path, "PNG", optimize=True)
    print("wrote", path, img.size)


if __name__ == "__main__":
    make_icon(ROOT / "sce_sys/icon0.png")
    make_background(ROOT / "sce_sys/background-source.png")
    make_background(ROOT / "sce_sys/launch-background-source.png")

