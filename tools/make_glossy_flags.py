"""Makes the "Glossy" flag set for the tray: bin_files/flags/Glossy/<language>/<country><size>.png.

The flags are GoSquared's "shiny" set (github.com/gosquared/flags, MIT, LICENSE-GoSquared.txt next to
them), the same the EveryLang switcher shows. GoSquared draws them by hand in 16, 24, 32, 48 and 64 px.

The tray asks for SM_CXSMICON at the system DPI (16 px at 100 %, 20 at 125 %, 24 at 150 % ...), and IconMgr
takes the PNG of exactly that size when there is one; a picture Windows scales itself is blurry. The sizes in
between are made here:
  - up to a quarter bigger than a drawing (20 from 16, 28 from 24, 36 and 40 from 32, 52, 56 and 60 from 48): that
    drawing with some rows and columns repeated - where a line equals its neighbours most (inside a stripe or a
    field), spread out. Edges and stripes stay on whole pixels: crisp. (Before 06.10.2026 these were scaled down
    from the next bigger drawing - Lanczos and a light sharpen - and came out soft: the stripes of a tricolour fell
    between pixels; Dmitry, at 125 %: "сделать флаги чётче".)
  - more than that (44): Lanczos from the next bigger drawing and a light sharpen.

    python tools/make_glossy_flags.py <a clone of github.com/gosquared/flags> [--preview preview.png]
    python tools/make_glossy_flags.py --in-place [--preview preview.png]
        the sizes in between again from the drawings already in the set (no clone needed)
"""
import argparse
import os
import shutil

from PIL import Image, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "bin_files", "flags", "Glossy")
NATIVE = [16, 24, 32, 48, 64]
SIZES = list(range(16, 65, 4))
REPEAT_UP_TO = 1.25  # how much bigger than a drawing a size is made by repeating lines
# Flags with diagonals, circles, crescents, stars and coats of arms: a repeated line breaks them into steps (looked
# at every flag at 20 px, 06.10.2026) - these are scaled down from the bigger drawing, as before.
SMOOTH = {"GB", "AU", "NZ", "CA", "ZA", "KG", "GE", "AZ", "HR", "BA", "MK", "TR", "PT", "BR", "SK", "SA", "IR", "IN",
          "BD", "PK", "VN", "MY", "CN", "HK", "KR", "MX", "RS", "AR"}

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


def _rows(img):
    w, h = img.size
    px = list(img.get_flattened_data() if hasattr(img, "get_flattened_data") else img.getdata())
    return [px[y * w:(y + 1) * w] for y in range(h)]


def _image(rows):
    img = Image.new("RGBA", (len(rows[0]), len(rows)))
    img.putdata([p for r in rows for p in r])
    return img


def _diff(a, b):
    return sum(abs(x - y) for p, q in zip(a, b) for x, y in zip(p, q))


def _repeat_rows(rows, k):
    """Repeat k rows of the flag, where a row equals its neighbours most (the repeat does not show), spread out; only
    rows opaque across the flag (a repeated soft edge would blur it)."""
    if k <= 0:
        return rows
    content = [i for i, r in enumerate(rows) if max(p[3] for p in r) > 0]
    lo, hi = content[0], content[-1]
    middle = rows[(lo + hi) // 2]
    across = [x for x, p in enumerate(middle) if p[3] >= 250]  # the flag, without the soft shadow at its ends
    cand = sorted((_diff(rows[i], rows[i + 1]) + 0.5 * _diff(rows[i], rows[i - 1]), i)
                  for i in range(lo + 1, hi - 1) if across and min(rows[i][x][3] for x in across) >= 250)
    spacing = max(1, (hi - lo + 1) // (k + 1) // 2)
    chosen = []
    for _, i in cand:
        if len(chosen) < k and all(abs(i - j) >= spacing for j in chosen):
            chosen.append(i)
    for _, i in cand:  # not enough far apart: the cheapest of the rest
        if len(chosen) < k and i not in chosen:
            chosen.append(i)
    out = []
    for i, r in enumerate(rows):
        out.append(r)
        if i in chosen:
            out.append(list(r))
    return out


def _transpose(rows):
    return [list(c) for c in zip(*rows)]


def repeat_lines(img, size):
    """The drawing img, its flag made size/img.width times bigger by repeating rows and columns, centred on a
    size x size picture."""
    x0, y0, x1, y1 = img.getbbox()
    scale = size / img.width
    rows = _repeat_rows(_rows(img), round((y1 - y0) * scale) - (y1 - y0))
    rows = _transpose(_repeat_rows(_transpose(rows), round((x1 - x0) * scale) - (x1 - x0)))
    flag = _image(rows)
    flag = flag.crop(flag.getbbox())
    out = Image.new("RGBA", (size, size))
    out.paste(flag, ((size - flag.width) // 2, (size - flag.height) // 2))
    return out


def repeats(cc, size):
    """The size in between is made by repeating lines of the smaller drawing (else - from the bigger one)."""
    return size not in NATIVE and cc not in SMOOTH and size / max(n for n in NATIVE if n < size) <= REPEAT_UP_TO


def make(native, size, cc):
    """The flag of `cc` at `size` px: native(n) - the drawing of n px (GoSquared's own), or made from a drawing."""
    if size in NATIVE:
        return native(size)
    if repeats(cc, size):
        return repeat_lines(native(max(n for n in NATIVE if n < size)), size)
    bigger = min(n for n in NATIVE if n > size)
    img = native(bigger).resize((size, size), Image.LANCZOS)
    return img.filter(ImageFilter.UnsharpMask(radius=0.6, percent=60, threshold=0))


def preview(native_of, path):
    """Russian and US flags at every size, on the dark and the light taskbar."""
    width = sum(SIZES) + 8 * len(SIZES) + 8
    sheet = Image.new("RGBA", (width, 2 * (64 + 16) * 2), (0, 0, 0, 255))
    for band, bg in enumerate([(28, 28, 28, 255), (243, 243, 243, 255)]):
        for row, (lang, cc) in enumerate([("ru", "RU"), ("en", "US")]):
            y = (band * 2 + row) * 80
            sheet.paste(bg, (0, y, width, y + 80))
            x = 8
            for size in SIZES:
                sheet.alpha_composite(make(native_of(lang, cc), size, cc), (x, y + 8))
                x += size + 8
    sheet.save(path)
    print(f"preview -> {path}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("gosquared", nargs="?")
    ap.add_argument("--in-place", action="store_true")
    ap.add_argument("--preview")
    args = ap.parse_args()
    if args.in_place:
        def native_of(lang, cc):
            return lambda n: Image.open(os.path.join(OUT, lang, f"{cc.lower()}{n}.png")).convert("RGBA")
        made = 0
        for lang, cc in LANGUAGES.items():
            for size in SIZES:
                if not repeats(cc, size):
                    continue  # a drawing, or from the bigger drawing: needs the clone, the file in the set stays
                make(native_of(lang, cc), size, cc).save(os.path.join(OUT, lang, f"{cc.lower()}{size}.png"),
                                                         optimize=True)
                made += 1
        print(f"{made} pictures made again -> {OUT}")
    else:
        if not args.gosquared:
            ap.error("a clone of github.com/gosquared/flags, or --in-place")
        source = os.path.join(args.gosquared, "flags", "flags-iso", "shiny")

        def native_of(lang, cc):
            return lambda n: Image.open(os.path.join(source, str(n), f"{cc}.png")).convert("RGBA")
        if os.path.isdir(OUT):
            shutil.rmtree(OUT)
        os.makedirs(OUT)
        shutil.copy(os.path.join(args.gosquared, "LICENSE.txt"), os.path.join(OUT, "LICENSE-GoSquared.txt"))
        for lang, cc in LANGUAGES.items():
            folder = os.path.join(OUT, lang)
            os.makedirs(folder)
            for size in SIZES:
                make(native_of(lang, cc), size, cc).save(os.path.join(folder, f"{cc.lower()}{size}.png"), optimize=True)
        print(f"{len(LANGUAGES)} languages x {len(SIZES)} sizes -> {OUT}")
    if args.preview:
        preview(native_of, args.preview)


if __name__ == "__main__":
    main()
