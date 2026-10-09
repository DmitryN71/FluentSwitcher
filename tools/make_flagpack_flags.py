"""Makes the flag set of FluentSwitcher: bin_files/flags/Flagpack/<language>/<country><size>.png, 16-64 px every 4.

The flags are Flagpack's (github.com/Yummygum/flagpack-core, MIT; tools/flagpack/LICENSE, copied next to the set as
LICENSE-Flagpack.txt). Flagpack draws each flag three times: 16x12 (s), 20x15 (m) and 32x24 (l) -
tools/flagpack/<s|m|l>/<country>.svg, only the countries of LANGUAGES (from
cdn.jsdelivr.net/gh/Yummygum/flagpack-core@main/svg/, GB - GB-UKM.svg, 07.10.2026). Dmitry chose the set over Pixel
Flags, flag-icons and country-flags (07.10.2026, tools/flags-compare in the work folder).

The tray asks for SM_CXSMICON at the system DPI (16 px at 100 %, 20 at 125 %, 24 at 150 % ...), and IconMgr takes the
PNG of exactly that size; the flag at the text cursor takes the nearest one. Each picture is square: the 4:3 flag
across its whole width, in the middle. A size is drawn from the SVG (16 - s, 20 - m, the rest - l) by Microsoft Edge
without a window (--headless, --dump-dom): Edge draws SVG as the browser page Dmitry chose the set on.

Stripes and crosses on whole pixels: a straight shape (a rect, a path of only M, H, V, Z) is drawn with
shape-rendering="crispEdges" - its edges go to the nearest pixel instead of a blurred one. Otherwise a stripe that
falls between pixels is soft and the stripes of one flag come out of different widths (Dmitry, 07.10.2026: "края
полосок справа и слева не выровнены"): the US has 13 stripes in 15 rows at 20 px. Coats of arms, stars, circles and
diagonals stay smooth.

The left and the right edge - in every row a point halfway between its own colour and light grey (SIDE, SIDE_TINT). An
LCD point is three stripes, red - green - blue from left to right: a red point shines on its left third, a blue one on
its right third, so at the flag's sides the stripes looked shifted by a third of a point (Dmitry at 125 %: white and
red stick out on the left, white on the right; a zoomed screenshot does not show it). A grey point shines on all three
alike: the sides are straight. Dmitry chose a light grey side of four variants (tools/flags-compare/edges.png in the
work folder), then, as "thinner", the half-grey one of six (edges2.png, 07.10.2026).

The US flag up to 32 px is drawn by points here, not by Flagpack (us_small). Flagpack's was blurred in the tray next to
the British one (forum, gutasiho, and Dmitry, 09.10.2026): its 50 stars made a checkerboard of blue and white points,
and 13 stripes in 15 or 18 rows came out one point and two points wide. Here the stripes are equal and odd in number
(red at the top and at the bottom), the stars - sparse white points. Dmitry chose this one of three variants
(tools/flags-compare/us_flags.png in the work folder, "B").

The second set - bin_files/flags/Waving/<language>/<country><size>.png, "Флаги с переливом" in the settings: the same
flags as Punto's glossy ones look (Punto with the forum's flags; Dmitry chose that look over three of our own,
09.10.2026, tools/flag-try in the work folder - a tray icon to compare them). Our drawing over our flags, measured by
Punto's 1049 point by point, not Punto's pictures: the body 14x10 in 16 (waving_dims), its colour pure and bright, folds
across it from Punto's (PUNTO_FOLDS: one row of them; each next row half a point to the left; scaled to the body), the
light - the colour with 40 % white, the dark - 60 % of it; a soft shadow a point right and down (fitted to Punto's
alpha). Drawn for every size, not one 16 stretched.

    python tools/make_flagpack_flags.py [--preview preview.png]
"""
import argparse
import base64
import colorsys
import io
import json
import os
import re
import shutil
import subprocess
import tempfile

from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "tools", "flagpack")
OUT = os.path.join(ROOT, "bin_files", "flags", "Flagpack")
OUT_WAVING = os.path.join(ROOT, "bin_files", "flags", "Waving")
EDGE = r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
SIZES = list(range(16, 65, 4))

# Language (as IconMgr looks it up: "en-us", then "en") -> the country of its flag.
LANGUAGES = {
    "en": "US", "en-gb": "GB", "en-au": "AU", "en-ca": "CA", "en-ie": "IE", "en-nz": "NZ", "en-za": "ZA",
    "ru": "RU", "uk": "UA", "be": "BY", "kk": "KZ", "uz": "UZ", "ky": "KG", "tg": "TJ", "tk": "TM",
    "az": "AZ", "hy": "AM", "ka": "GE", "ro": "RO", "bg": "BG", "sr": "RS", "hr": "HR", "bs": "BA",
    "sl": "SI", "mk": "MK", "sq": "AL", "el": "GR", "tr": "TR",
    "de": "DE", "de-at": "AT", "de-ch": "CH", "fr": "FR", "fr-ca": "CA", "fr-be": "BE", "fr-ch": "CH",
    "it": "IT", "es": "ES", "es-mx": "MX", "es-ar": "AR", "pt": "PT", "pt-br": "BR", "nl": "NL", "nl-be": "BE",
    "da": "DK", "sv": "SE", "nb": "NO", "nn": "NO", "no": "NO", "fi": "FI", "is": "IS", "ga": "IE",
    "et": "EE", "lv": "LV", "lt": "LT", "pl": "PL", "cs": "CZ", "sk": "SK", "hu": "HU",
    "he": "IL", "ar": "SA", "fa": "IR", "hi": "IN", "bn": "BD", "ur": "PK", "th": "TH", "vi": "VN",
    "id": "ID", "ms": "MY", "zh": "CN", "zh-tw": "TW", "zh-hk": "HK", "ja": "JP", "ko": "KR", "mn": "MN",
}

STRAIGHT_PATH = re.compile(r'\sd="[MmHhVvZz0-9eE.,\s-]*"')


def crisp(text):
    """The SVG with its straight shapes (rects and paths of M, H, V, Z, not turned) drawn on whole pixels."""
    def tag(m):
        t = m.group(0)
        if "transform" in t or "shape-rendering" in t:
            return t
        if t.startswith("<rect") or (t.startswith("<path") and STRAIGHT_PATH.search(t)):
            return t[:5] + ' shape-rendering="crispEdges"' + t[5:]
        return t
    return re.sub(r"<(?:rect|path)\b[^>]*>", tag, text)


def render_all(countries):
    """RGBA pictures drawn by Edge: (country, size) - the flag, size x size*3/4; (country, size, "waving") - the body of
    the waving one (waving_dims)."""
    jobs, sources = [], {}
    for cc in countries:
        for size in SIZES:
            kind = "s" if size == 16 else "m" if size == 20 else "l"
            key = f"{cc}/{kind}"
            if key not in sources:
                text = crisp(open(os.path.join(SRC, kind, f"{cc}.svg"), encoding="utf-8").read())
                sources[key] = "data:image/svg+xml;base64," + base64.b64encode(text.encode()).decode()
            jobs.append({"id": f"{cc}|{size}|", "src": key, "w": size, "h": size * 3 // 4})
            w, h, _ = waving_dims(size)
            jobs.append({"id": f"{cc}|{size}|waving", "src": key, "w": w, "h": h})
    page = """<!doctype html><meta charset="utf-8"><body><pre id="out"></pre><script>
const SOURCES = %s, JOBS = %s;
(async () => {
  const imgs = {};
  for (const [k, v] of Object.entries(SOURCES)) { const i = new Image(); i.src = v; await i.decode(); imgs[k] = i; }
  const out = [];
  for (const j of JOBS) {
    const c = document.createElement('canvas'); c.width = j.w; c.height = j.h;
    c.getContext('2d').drawImage(imgs[j.src], 0, 0, j.w, j.h);
    out.push(j.id + '|' + c.toDataURL('image/png'));
  }
  document.getElementById('out').textContent = '@@BEGIN@@\\n' + out.join('\\n') + '\\n@@END@@';
})();
</script>""" % (json.dumps(sources), json.dumps(jobs))
    work = tempfile.mkdtemp(prefix="fs-flags-")
    try:
        path = os.path.join(work, "render.html")
        open(path, "w", encoding="utf-8").write(page)
        dom = subprocess.run([EDGE, "--headless=new", "--disable-gpu", "--no-first-run", "--no-default-browser-check",
                              f"--user-data-dir={os.path.join(work, 'profile')}", "--virtual-time-budget=60000",
                              "--dump-dom", "file:///" + path.replace("\\", "/")],
                             capture_output=True, text=True, encoding="utf-8", timeout=300).stdout
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if "@@END@@" not in dom:
        raise SystemExit("Edge did not draw the flags")
    body = dom[dom.index("@@BEGIN@@") + len("@@BEGIN@@"):dom.index("@@END@@")]
    out = {}
    for line in body.strip().splitlines():
        cc, size, tag, url = line.split("|", 3)
        out[(cc, int(size), tag) if tag else (cc, int(size))] = \
            Image.open(io.BytesIO(base64.b64decode(url.split(",", 1)[1]))).convert("RGBA")
    if len(out) != len(jobs):
        raise SystemExit(f"Edge drew {len(out)} of {len(jobs)} pictures")
    return out


US_RED, US_WHITE, US_BLUE = (227, 29, 28, 255), (247, 252, 255, 255), (46, 66, 165, 255)  # Flagpack's colours
# Size -> (stripes, points each): one point at 16 and 20, two from 24.
US_STRIPES = {16: (11, 1), 20: (15, 1), 24: (9, 2), 28: (11, 2), 32: (13, 2)}


def us_small(size, width=None):
    """The US flag by points: equal stripes, the canton over the upper half of them (7 of 13) and about 57 % of the
    width (as on the flag: 0.76 of the height of 13 stripes), in it white points a step apart - one in four. width -
    narrower than the size (the waving flag's body)."""
    n, t = US_STRIPES[size]
    width = width or size
    flag = Image.new("RGBA", (width, n * t), US_WHITE)
    d = ImageDraw.Draw(flag)
    for i in range(0, n, 2):
        d.rectangle([0, i * t, width - 1, i * t + t - 1], fill=US_RED)
    ch, cw = (n // 2 + 1) * t, round(width * 0.57)
    d.rectangle([0, 0, cw - 1, ch - 1], fill=US_BLUE)
    step = 2 if t == 1 else 3
    for y in range(1, ch - 1, step):
        for x in range(1 + (y // step % 2), cw - 1, step):
            flag.putpixel((x, y), US_WHITE)
    return flag


# Punto's glossy 1049 by points (body 14x10): how light its first row is, column by column (0 - the darkest fold,
# 1 - the lightest), and further right; each next row is the same, half a point to the left.
PUNTO_FOLDS = [0.31, 0.62, 0.85, 0.99, 1.0, 0.88, 0.66, 0.34, 0.06, 0.0, 0.16, 0.48, 0.77, 0.95, 1.0, 0.92, 0.72, 0.45,
               0.2, 0.05, 0.0]


def folds(u):
    u = max(0.0, min(len(PUNTO_FOLDS) - 1.0, u))
    i = min(int(u), len(PUNTO_FOLDS) - 2)
    return PUNTO_FOLDS[i] + (PUNTO_FOLDS[i + 1] - PUNTO_FOLDS[i]) * (u - i)


def waving_dims(size):
    """The waving flag's body (width, height) and the room for its shadow: 14x10 and 2 at 16, as Punto's."""
    shadow = round(size / 8)
    w = size - shadow
    return w, round(w / 1.4), shadow


def waving(body, size):
    """The waving flag in a square picture: the body's colour pure and bright (saturation x1.35, brightness x1.55),
    Punto's folds over it, the light - the colour with 40 % white, the dark - 60 % of it; under it a shadow."""
    w, h = body.size
    img = body.copy()
    px = img.load()
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            hh, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
            if s > 0.15:
                s = min(1.0, s * 1.35)
            c = [255 * k for k in colorsys.hsv_to_rgb(hh, s, min(1.0, v * 1.55))]
            t = folds((x + 0.5) * 14 / w - 0.5 + 0.5 * ((y + 0.5) * 10 / h - 0.5))
            light = [k + 0.4 * (255 - k) for k in c]
            dark = [0.6 * k for k in c]
            px[x, y] = tuple(round(d + (l - d) * t) for d, l in zip(dark, light)) + (a,)
    _, _, shadow = waving_dims(size)
    gap = size - h - shadow
    top = gap - gap // 4  # Punto's at 16: three rows above, one below
    # The shadow: the body a point right and down, blurred by a point, 144 strong - fitted to Punto's at 16 (106 / 38
    # right, 104 / 35 below, softer at the corners). Drawn 8 times larger and made smaller, so that at 20, 28 px it
    # moves by parts of a point.
    k = 8
    mask = Image.new("L", (size * k, size * k))
    step = size / 16 * k
    mask.paste(Image.new("L", (w * k, h * k), 144), (round(step), round(top * k + step)))
    mask = mask.filter(ImageFilter.BoxBlur(step)).resize((size, size), Image.BOX)
    square_ = Image.new("RGBA", (size, size))
    square_.paste((0, 0, 0, 255), (0, 0), mask)
    square_.alpha_composite(img, (0, top))
    return square_


SIDE = 200        # light grey
SIDE_TINT = 0.5   # how much of the point's own colour stays in it


def sides(flag):
    """The flag with the first and the last point of every row halfway to light grey (SIDE, SIDE_TINT)."""
    flag = flag.copy()
    px = flag.load()
    w, h = flag.size
    for y in range(h):
        xs = [x for x in range(w) if px[x, y][3] > 0]
        for x in {xs[0], xs[-1]} if xs else ():
            r, g, b, a = px[x, y]
            px[x, y] = tuple(round(SIDE + (v - SIDE) * SIDE_TINT) for v in (r, g, b)) + (a,)
    return flag


def square(flag, size):
    out = Image.new("RGBA", (size, size))
    out.paste(sides(flag), (0, (size - flag.height) // 2))
    return out


def preview(drawn, path):
    """Some flags at every size on the dark and the light taskbar."""
    show = ["RU", "US", "UA", "DE", "GB", "KZ", "FR", "PL"]
    width = sum(SIZES) + 8 * len(SIZES) + 8
    sheet = Image.new("RGBA", (width, 2 * len(show) * 72), (0, 0, 0, 255))
    for band, bg in enumerate([(28, 28, 28, 255), (238, 240, 243, 255)]):
        for row, cc in enumerate(show):
            y = (band * len(show) + row) * 72
            sheet.paste(bg, (0, y, width, y + 72))
            x = 8
            for size in SIZES:
                sheet.alpha_composite(square(drawn[(cc, size)], size), (x, y + 4))
                sheet.alpha_composite(waving(drawn[(cc, size, "waving")], size), (x, y + 4 + size + 2))
                x += size + 8
    sheet.save(path)
    print(f"preview -> {path}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview")
    args = ap.parse_args()
    countries = sorted(set(LANGUAGES.values()))
    drawn = render_all(countries)
    for size in US_STRIPES:
        drawn[("US", size)] = us_small(size)
        drawn[("US", size, "waving")] = us_small(size, waving_dims(size)[0])
    for out, picture in ((OUT, lambda cc, size: square(drawn[(cc, size)], size)),
                         (OUT_WAVING, lambda cc, size: waving(drawn[(cc, size, "waving")], size))):
        if os.path.isdir(out):
            shutil.rmtree(out)
        os.makedirs(out)
        shutil.copy(os.path.join(SRC, "LICENSE"), os.path.join(out, "LICENSE-Flagpack.txt"))
        for lang, cc in LANGUAGES.items():
            folder = os.path.join(out, lang)
            os.makedirs(folder)
            for size in SIZES:
                picture(cc, size).save(os.path.join(folder, f"{cc.lower()}{size}.png"), optimize=True)
        print(f"{len(LANGUAGES)} languages x {len(SIZES)} sizes -> {out}")
    if args.preview:
        preview(drawn, args.preview)


if __name__ == "__main__":
    main()
