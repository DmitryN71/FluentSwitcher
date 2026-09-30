"""Draws the "Fluent" flag set for the tray: bin_files/flags/Fluent/<lang>/<size>.png.

The tray asks for SM_CXSMICON at the system DPI (16 px at 100 %, 20 at 125 %, 24 at 150 % ...), and
IconMgr takes the PNG of exactly that size when there is one. A picture scaled by Windows from another
size is blurry, so every size from 16 to 64 px in steps of 4 is drawn separately, on whole pixels:
stripes end on pixel borders, only the rounded corners are smoothed. Each flag gets a thin darker edge
so that white stripes stay visible on a light taskbar.

    python tools/make_fluent_flags.py [--preview preview.png]
"""
import argparse
import math
import os

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "bin_files", "flags", "Fluent")
SIZES = list(range(16, 65, 4))

RU_WHITE, RU_BLUE, RU_RED = (255, 255, 255), (0, 57, 166), (213, 43, 30)
US_RED, US_WHITE, US_BLUE = (178, 34, 52), (255, 255, 255), (60, 59, 110)

# Flag height and the number of US stripes, chosen so that the stripes are whole pixels.
# Above 40 px (250 % and more) pixels are small enough for stripes of 2 and 3 px to look even.
FIXED = {16: (11, 11), 20: (13, 13), 24: (18, 9), 28: (18, 9), 32: (22, 11), 36: (26, 13), 40: (26, 13)}


def geometry(size):
    if size in FIXED:
        return FIXED[size]
    return round(size * 0.68), 13


def rows(height, count):
    """Pixel borders of `count` stripes over `height` rows."""
    return [round(i * height / count) for i in range(count + 1)]


def rounded_coverage(width, height, radius, inset=0.0, scale=8):
    """How much of each pixel lies inside the rounded rectangle, 0..1 (supersampled)."""
    cov = [[0.0] * width for _ in range(height)]
    r = max(radius - inset, 0.0)
    x0, y0, x1, y1 = inset, inset, width - inset, height - inset
    step = 1.0 / scale
    for y in range(height):
        for x in range(width):
            inside = 0
            for sy in range(scale):
                py = y + (sy + 0.5) * step
                for sx in range(scale):
                    px = x + (sx + 0.5) * step
                    if px < x0 or px > x1 or py < y0 or py > y1:
                        continue
                    cx = min(max(px, x0 + r), x1 - r)
                    cy = min(max(py, y0 + r), y1 - r)
                    if (px - cx) ** 2 + (py - cy) ** 2 <= r * r:
                        inside += 1
            cov[y][x] = inside / (scale * scale)
    return cov


def draw_ru(width, height):
    img = Image.new("RGB", (width, height))
    d = ImageDraw.Draw(img)
    top = round(height / 3)
    bottom = height - top
    d.rectangle([0, 0, width - 1, top - 1], fill=RU_WHITE)
    d.rectangle([0, top, width - 1, bottom - 1], fill=RU_BLUE)
    d.rectangle([0, bottom, width - 1, height - 1], fill=RU_RED)
    return img


def draw_us(width, height, stripes, size):
    img = Image.new("RGB", (width, height))
    d = ImageDraw.Draw(img)
    border = rows(height, stripes)
    for i in range(stripes):
        d.rectangle([0, border[i], width - 1, border[i + 1] - 1], fill=US_RED if i % 2 == 0 else US_WHITE)
    cw = round(width * 0.42)
    ch = border[(stripes + 1) // 2]
    d.rectangle([0, 0, cw - 1, ch - 1], fill=US_BLUE)
    if size <= 32:
        # Stars as single white pixels in a staggered grid: smoothed dots this small turn into a grey blur.
        for y in range(1, ch - 1, 2):
            shift = 0 if (y // 2) % 2 == 0 else 1
            for x in range(1 + shift, cw - 1, 2):
                img.putpixel((x, y), US_WHITE)
        return img
    # Stars as small round dots in a staggered grid, smoothed.
    big = 8
    layer = Image.new("L", (cw * big, ch * big), 0)
    ld = ImageDraw.Draw(layer)
    rows_n = max(3, round(ch / (size / 9)))
    cols_n = max(3, round(cw / (size / 9)))
    dot = max(1.0, size / 22) * big / 2
    for r in range(rows_n):
        cy = (r + 0.5) * ch * big / rows_n
        odd = r % 2
        n = cols_n - odd
        for c in range(n):
            cx = (c + 0.5 + odd * 0.5) * cw * big / cols_n
            ld.ellipse([cx - dot, cy - dot, cx + dot, cy + dot], fill=255)
    mask = layer.resize((cw, ch), Image.BOX)
    img.paste(Image.new("RGB", (cw, ch), US_WHITE), (0, 0), mask)
    return img


def finish(flag, size):
    """The flag centred in a transparent size x size square, corners rounded, a thin darker edge."""
    width, height = flag.size
    radius = max(1.6, size / 10)
    outer = rounded_coverage(width, height, radius)
    inner = rounded_coverage(width, height, radius, inset=1.0)
    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    top = (size - height) // 2
    left = (size - width) // 2
    for y in range(height):
        for x in range(width):
            a = outer[y][x]
            if a <= 0:
                continue
            edge = max(a - inner[y][x], 0.0) / a  # share of the pixel that is the edge
            k = 1.0 - 0.30 * edge
            r, g, b = flag.getpixel((x, y))
            out.putpixel((left + x, top + y), (round(r * k), round(g * k), round(b * k), round(255 * a)))
    return out


def render(lang, size):
    height, stripes = geometry(size)
    flag = draw_ru(size, height) if lang == "ru" else draw_us(size, height, stripes, size)
    return finish(flag, size)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", help="also save a preview sheet (dark and light taskbar, 8x zoom)")
    args = ap.parse_args()
    made = {}
    for lang in ("ru", "en"):
        folder = os.path.join(OUT, lang)
        os.makedirs(folder, exist_ok=True)
        for size in SIZES:
            img = render(lang, size)
            img.save(os.path.join(folder, f"{lang}{size}.png"))
            made[(lang, size)] = img
    print(f"{len(made)} files in {OUT}")
    if args.preview:
        preview(made, args.preview)


def preview(made, path):
    zoom_sizes = [16, 20, 24, 32]
    zoom = 8
    pad = 24
    band_h = 64 + pad * 2
    width = pad + sum(s * zoom + pad for s in zoom_sizes) * 2
    height = band_h * 2 + max(zoom_sizes) * zoom + pad * 2
    sheet = Image.new("RGBA", (width, height), (40, 40, 40, 255))
    # Real size on a dark and a light taskbar.
    for band, bg in enumerate([(28, 28, 28, 255), (243, 243, 243, 255)]):
        y0 = band * band_h
        ImageDraw.Draw(sheet).rectangle([0, y0, width, y0 + band_h - 1], fill=bg)
        x = pad
        for size in SIZES:
            for lang in ("en", "ru"):
                img = made[(lang, size)]
                sheet.alpha_composite(img, (x, y0 + (band_h - size) // 2))
                x += size + 8
            x += 12
    # Zoomed, to check the pixels.
    y = band_h * 2 + pad
    x = pad
    for lang in ("en", "ru"):
        for size in zoom_sizes:
            img = made[(lang, size)]
            big = img.resize((size * zoom, size * zoom), Image.NEAREST)
            bg = Image.new("RGBA", big.size, (28, 28, 28, 255))
            bg.alpha_composite(big)
            sheet.alpha_composite(bg, (x, y))
            x += size * zoom + pad
    sheet.save(path)
    print("preview:", path)


if __name__ == "__main__":
    main()
