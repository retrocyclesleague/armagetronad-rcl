#!/usr/bin/env python3
"""Rasterise Space Grotesk into the UI font atlases the client draws text with.

The client has no font rasteriser. It draws interface text from prebuilt
atlases, one per weight and pixel size, and always picks a size it can draw
texel for pixel, so text stays sharp at any resolution. This script builds
those atlases and the metrics file that describes them:

    textures/ui/space-grotesk-<weight>-<px>.png   white glyphs, coverage in alpha
    textures/ui/space-grotesk.fnt                 faces, glyph boxes, advances
    textures/icon.png                             the window icon: the wordmark

Source font: scripts/assets/fonts/space-grotesk/space-grotesk-latin.woff2, the
file the RCL site serves (SIL OFL 1.1, see OFL.txt next to it).

    python3 scripts/generate-rcl-ui-font.py

Needs Pillow built with FreeType and Brotli (the standard wheels are).
"""

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "scripts" / "assets" / "fonts" / "space-grotesk" / "space-grotesk-latin.woff2"
OUT = ROOT / "textures" / "ui"

# The engine's text is single-byte. 32-126 and 160-255 are Latin-1; 128-159
# follow Windows-1252 where the font has the glyph (ellipsis, dashes, quotes,
# angle quotes, bullet, euro, trade mark).
CP1252_HIGH = {
    0x80: 0x20AC, 0x82: 0x201A, 0x84: 0x201E, 0x85: 0x2026, 0x86: 0x2020,
    0x87: 0x2021, 0x88: 0x02C6, 0x89: 0x2030, 0x8B: 0x2039, 0x8C: 0x0152,
    0x91: 0x2018, 0x92: 0x2019, 0x93: 0x201C, 0x94: 0x201D, 0x95: 0x2022,
    0x96: 0x2013, 0x97: 0x2014, 0x98: 0x02DC, 0x99: 0x2122, 0x9B: 0x203A,
    0x9C: 0x0153,
}


def ladder(first, last):
    """Pixel sizes close enough together that any size snaps within ~5%."""
    sizes = list(range(10, 25)) + list(range(26, 41, 2)) + list(range(44, 65, 4)) \
        + list(range(72, 97, 8))
    return [s for s in sizes if first <= s <= last]


# weight -> (pixel sizes, characters); None means the full set
FACES = {
    400: (ladder(10, 64), None),       # body, values, help, console, chat
    500: (ladder(11, 72), None),       # labels, navigation, buttons, headings
    600: (ladder(13, 96), None),       # titles, emphasised numbers
    700: (ladder(14, 96), "rcl"),      # the wordmark only
}


def codepoints(only):
    table = {}
    for code in list(range(32, 127)) + list(range(160, 256)):
        table[code] = code
    table.update(CP1252_HIGH)
    if only is not None:
        table = {code: cp for code, cp in table.items() if chr(cp) in only}
    return table


def rasterise(font, size, char):
    """Glyph bitmap, its offset from the pen position, and its advance."""
    pad = size
    ascent, descent = font.getmetrics()
    advance = font.getlength(char)
    canvas = Image.new("L", (int(advance) + 2 * pad + 2, ascent + descent + 2 * pad), 0)
    baseline = pad + ascent
    ImageDraw.Draw(canvas).text((pad, baseline), char, font=font, fill=255, anchor="ls")
    box = canvas.getbbox()
    if box is None:
        return None, 0, 0, advance
    glyph = canvas.crop(box)
    # bearing x from the pen, bearing y up from the baseline to the bitmap top
    return glyph, box[0] - pad, baseline - box[1], advance


def build_face(weight, size, only):
    font = ImageFont.truetype(str(SOURCE), size, layout_engine=ImageFont.Layout.BASIC)
    font.set_variation_by_axes([weight])
    ascent, descent = font.getmetrics()

    glyphs = {}
    for code, cp in sorted(codepoints(only).items()):
        if code != 32 and code != 160 and font.getmask(chr(cp)).getbbox() is None \
                and font.getlength(chr(cp)) == 0:
            continue
        glyphs[code] = rasterise(font, size, chr(cp))

    # Tabular figures: every digit advances by the widest digit and sits
    # centred in it, so numbers do not shift as they change.
    digits = [c for c in range(ord("0"), ord("9") + 1) if c in glyphs]
    if digits:
        widest = max(glyphs[c][3] for c in digits)
        for c in digits:
            image, bx, by, advance = glyphs[c]
            glyphs[c] = (image, bx + int(round((widest - advance) / 2)), by, widest)

    # shelf-pack, tallest first, one transparent pixel around every glyph
    order = sorted((c for c in glyphs if glyphs[c][0] is not None),
                   key=lambda c: -glyphs[c][0].height)
    area = sum((glyphs[c][0].width + 2) * (glyphs[c][0].height + 2) for c in order)
    width = 64
    while width * width < area * 1.25:
        width *= 2
    while True:
        x = y = shelf = 0
        places = {}
        for c in order:
            w, h = glyphs[c][0].width + 2, glyphs[c][0].height + 2
            if x + w > width:
                x, y, shelf = 0, y + shelf, 0
            places[c] = (x + 1, y + 1)
            x += w
            shelf = max(shelf, h)
        height = 64
        while height < y + shelf:
            height *= 2
        if height <= width:
            break
        width *= 2

    alpha = Image.new("L", (width, height), 0)
    for c in order:
        alpha.paste(glyphs[c][0], places[c])
    atlas = Image.merge("LA", (Image.new("L", (width, height), 255), alpha))
    name = "space-grotesk-%d-%d.png" % (weight, size)
    atlas.save(OUT / name, optimize=True)

    lines = ["face %d %d %d %d %d %d %d %s" % (
        weight, size, ascent, descent, width, height, len(glyphs), name)]
    for code in sorted(glyphs):
        image, bx, by, advance = glyphs[code]
        x, y = places.get(code, (0, 0))
        w, h = (image.width, image.height) if image is not None else (0, 0)
        lines.append("g %d %d %d %d %d %d %d %.3f" % (code, x, y, w, h, bx, by, advance))
    return lines, (OUT / name).stat().st_size


def write_icon():
    """The window icon: the lowercase wordmark over its lime underline."""
    size = 64
    icon = Image.new("RGB", (size, size), (0x20, 0x21, 0x22))
    draw = ImageDraw.Draw(icon)
    font = ImageFont.truetype(str(SOURCE), 34, layout_engine=ImageFont.Layout.BASIC)
    font.set_variation_by_axes([700])
    left, top, right, bottom = draw.textbbox((0, 0), "rcl", font=font)
    x = (size - (right - left)) // 2 - left
    y = 10 - top
    draw.text((x, y), "rcl", font=font, fill=(0xFA, 0xFA, 0xFA))
    bar = y + bottom + 6
    draw.rectangle((x + left, bar, x + right - 1, bar + 5), fill=(0xEE, 0xFF, 0x41))
    icon.save(ROOT / "textures" / "icon.png", optimize=True)


def main():
    write_icon()
    OUT.mkdir(parents=True, exist_ok=True)
    for stale in OUT.glob("space-grotesk-*.png"):
        stale.unlink()
    lines, total, count = ["RCLFONT 1"], 0, 0
    for weight, (sizes, only) in sorted(FACES.items()):
        for size in sizes:
            face, size_on_disk = build_face(weight, size, only)
            lines += face
            total += size_on_disk
            count += 1
    (OUT / "space-grotesk.fnt").write_text("\n".join(lines) + "\n", newline="\n")
    print("%d faces, %.1f MB of atlases" % (count, total / 1e6))


if __name__ == "__main__":
    main()
