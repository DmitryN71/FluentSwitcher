"""Makes the FluentSwitcher icon: settings/app.ico (the settings window) and src/res/app.ico (the engine).

A keyboard with a "switch" badge, in the style of FluentClipper's icon: glyphs of Microsoft's Fluent UI
System Icons (github.com/microsoft/fluentui-system-icons, MIT, see fluentui/LICENSE-FluentUI-System-Icons.txt)
in the dark theme's accent colour, the badge cut out of the keyboard. 16 and 20 px: the keyboard alone
(its own small-size drawing), a badge that small cannot be read.

The SVGs are rendered by the NanoSVG code wxWidgets uses at run time: svg2png.exe, a wx program that takes
a list of "<svg>|<size>|<png>" lines (FluentClipper's icons/tools/svg2png.cpp).

    python tools/make_app_icon.py <folder of @fluentui/svg-icons: the one with icons/*.svg> <svg2png.exe>
"""
import io
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw

COLOUR = "#60cdff"
SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
BADGE = 0.5          # the badge's box, of the icon's size
GAP = 1.0            # the gap around the badge's disc, in 24 px units

HERE = Path(__file__).resolve().parent.parent          # ...\settings
OUTS = [HERE / "app.ico", HERE.parent / "src" / "res" / "app.ico"]


def badge_svg(px):
    """The drawing of the badge made for the nearest size."""
    for s in (16, 20, 24, 28, 32, 48):
        if px <= s:
            return f"arrow_sync_circle_{s}_filled"
    return "arrow_sync_circle_48_filled"


def main():
    icons = Path(sys.argv[1]) / "icons"
    renderer = Path(sys.argv[2])
    work = Path(tempfile.mkdtemp(prefix="fs-icon-"))

    jobs = []
    for s in SIZES:
        jobs.append((f"keyboard_{s if s in (16, 20) else 24}_regular", s))
        if s > 20:
            b = round(s * BADGE)
            jobs.append((badge_svg(b), b))
    lines = []
    for name, px in jobs:
        svg_text = (icons / f"{name}.svg").read_text(encoding="utf-8")
        box = re.search(r'viewBox="([^"]+)"', svg_text).group(1)
        body = "".join(f'<path d="{d}"/>' for d in re.findall(r' d="([^"]+)"', svg_text))
        svg = work / f"{name}_{px}.svg"
        svg.write_text(f'<svg xmlns="http://www.w3.org/2000/svg" width="{px}" height="{px}" viewBox="{box}" '
                       f'fill="{COLOUR}">{body}</svg>', encoding="utf-8")
        lines.append(f"{svg}|{px}|{work / f'{name}_{px}.png'}")
    # svg2png stops before the list's last line: a blank line at the end.
    (work / "render.lst").write_text("\n".join(lines) + "\n\n", encoding="utf-8")
    subprocess.run([str(renderer), str(work / "render.lst")], cwd=work, check=False, timeout=60)

    def png(name, px):
        f = work / f"{name}_{px}.png"
        if not f.exists():
            sys.exit(f"not rendered: {f}")
        return Image.open(f).convert("RGBA")

    images = []
    for s in SIZES:
        img = png(f"keyboard_{s if s in (16, 20) else 24}_regular", s)
        if s > 20:
            b = round(s * BADGE)
            badge = png(badge_svg(b), b)
            x = y = s - b
            # The badge's disc: radius 10 of its 24 units, in the middle of its box.
            cx, cy, r = x + b / 2, y + b / 2, b * 10 / 24 + GAP * s / 24
            cut = Image.new("L", (s, s))
            ImageDraw.Draw(cut).ellipse([cx - r, cy - r, cx + r, cy + r], fill=255)
            img.putalpha(ImageChops.multiply(img.getchannel("A"), ImageChops.invert(cut)))
            img.alpha_composite(badge, (x, y))
        data = io.BytesIO()
        img.save(data, "PNG")
        images.append((s, data.getvalue()))

    # ICO: header, one directory entry per image, then the PNG data.
    header = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries, blobs = b"", b""
    for size, data in images:
        dim = 0 if size >= 256 else size
        entries += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
        blobs += data
    for out in OUTS:
        out.write_bytes(header + entries + blobs)
        print(f"wrote {out} ({len(images)} sizes, {out.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
