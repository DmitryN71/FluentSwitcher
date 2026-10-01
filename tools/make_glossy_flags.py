"""Makes the "Glossy" flag set for the tray: bin_files/flags/Glossy/<language>/<country><size>.png.

The flags are GoSquared's "shiny" set (github.com/gosquared/flags, MIT, LICENSE-GoSquared.txt next to
them), the same the EveryLang switcher shows. GoSquared draws them by hand in 16, 24, 32, 48 and 64 px.

The tray asks for SM_CXSMICON at the system DPI (16 px at 100 %, 20 at 125 %, 24 at 150 % ...), and IconMgr
takes the PNG of exactly that size when there is one; a picture Windows scales itself is blurry. So the
sizes in between (20, 28, 36 ...) are made here from the next bigger drawing: Lanczos and a light sharpen.

    python tools/make_glossy_flags.py <a clone of github.com/gosquared/flags> [--preview preview.png]
"""
import argparse
import os
import shutil

from PIL import Image, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "bin_files", "flags", "Glossy")
NATIVE = [16, 24, 32, 48, 64]
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


def make(source, cc, size):
    """The flag of country `cc` at `size` px: GoSquared's own drawing, or made from the next bigger one."""
    if size in NATIVE:
        return Image.open(os.path.join(source, str(size), f"{cc}.png")).convert("RGBA")
    bigger = min(n for n in NATIVE if n > size)
    img = Image.open(os.path.join(source, str(bigger), f"{cc}.png")).convert("RGBA")
    img = img.resize((size, size), Image.LANCZOS)
    return img.filter(ImageFilter.UnsharpMask(radius=0.6, percent=60, threshold=0))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("gosquared")
    ap.add_argument("--preview")
    args = ap.parse_args()
    source = os.path.join(args.gosquared, "flags", "flags-iso", "shiny")

    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    os.makedirs(OUT)
    shutil.copy(os.path.join(args.gosquared, "LICENSE.txt"), os.path.join(OUT, "LICENSE-GoSquared.txt"))
    for lang, cc in LANGUAGES.items():
        folder = os.path.join(OUT, lang)
        os.makedirs(folder)
        for size in SIZES:
            make(source, cc, size).save(os.path.join(folder, f"{cc.lower()}{size}.png"), optimize=True)
    print(f"{len(LANGUAGES)} languages x {len(SIZES)} sizes -> {OUT}")

    if args.preview:
        # Russian and US flags at every size, on the dark and the light taskbar.
        width = sum(SIZES) + 8 * len(SIZES) + 8
        sheet = Image.new("RGBA", (width, 2 * (64 + 16) * 2), (0, 0, 0, 255))
        for band, bg in enumerate([(28, 28, 28, 255), (243, 243, 243, 255)]):
            for row, cc in enumerate(["RU", "US"]):
                y = (band * 2 + row) * 80
                sheet.paste(bg, (0, y, width, y + 80))
                x = 8
                for size in SIZES:
                    sheet.alpha_composite(make(source, cc, size), (x, y + 8))
                    x += size + 8
        sheet.save(args.preview)
        print(f"preview -> {args.preview}")


if __name__ == "__main__":
    main()
