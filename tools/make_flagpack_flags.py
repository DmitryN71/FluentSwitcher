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

    python tools/make_flagpack_flags.py [--preview preview.png]
"""
import argparse
import base64
import io
import json
import os
import re
import shutil
import subprocess
import tempfile

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "tools", "flagpack")
OUT = os.path.join(ROOT, "bin_files", "flags", "Flagpack")
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
    """{(country, size): RGBA picture of the flag, size x size*3/4}, drawn by Edge."""
    jobs, sources = [], {}
    for cc in countries:
        for size in SIZES:
            kind = "s" if size == 16 else "m" if size == 20 else "l"
            key = f"{cc}/{kind}"
            if key not in sources:
                text = crisp(open(os.path.join(SRC, kind, f"{cc}.svg"), encoding="utf-8").read())
                sources[key] = "data:image/svg+xml;base64," + base64.b64encode(text.encode()).decode()
            jobs.append({"cc": cc, "size": size, "src": key, "w": size, "h": size * 3 // 4})
    page = """<!doctype html><meta charset="utf-8"><body><pre id="out"></pre><script>
const SOURCES = %s, JOBS = %s;
(async () => {
  const imgs = {};
  for (const [k, v] of Object.entries(SOURCES)) { const i = new Image(); i.src = v; await i.decode(); imgs[k] = i; }
  const out = [];
  for (const j of JOBS) {
    const c = document.createElement('canvas'); c.width = j.w; c.height = j.h;
    c.getContext('2d').drawImage(imgs[j.src], 0, 0, j.w, j.h);
    out.push(j.cc + '|' + j.size + '|' + c.toDataURL('image/png'));
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
        cc, size, url = line.split("|", 2)
        out[(cc, int(size))] = Image.open(io.BytesIO(base64.b64decode(url.split(",", 1)[1]))).convert("RGBA")
    if len(out) != len(countries) * len(SIZES):
        raise SystemExit(f"Edge drew {len(out)} of {len(countries) * len(SIZES)} pictures")
    return out


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
                x += size + 8
    sheet.save(path)
    print(f"preview -> {path}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview")
    args = ap.parse_args()
    countries = sorted(set(LANGUAGES.values()))
    drawn = render_all(countries)
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    os.makedirs(OUT)
    shutil.copy(os.path.join(SRC, "LICENSE"), os.path.join(OUT, "LICENSE-Flagpack.txt"))
    for lang, cc in LANGUAGES.items():
        folder = os.path.join(OUT, lang)
        os.makedirs(folder)
        for size in SIZES:
            square(drawn[(cc, size)], size).save(os.path.join(folder, f"{cc.lower()}{size}.png"), optimize=True)
    print(f"{len(LANGUAGES)} languages x {len(SIZES)} sizes -> {OUT}")
    if args.preview:
        preview(drawn, args.preview)


if __name__ == "__main__":
    main()
