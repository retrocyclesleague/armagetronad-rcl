#!/usr/bin/env python3
"""Export the RCL vector identity to native application assets.

Space Grotesk outlines and the staging kit's graphite/paper/lime palette.
Requires Pillow and fontTools; no external artwork or runtime dependencies.
The SVG files are the resolution-independent source, including outlined type.
"""
from pathlib import Path
from html import escape
from PIL import Image, ImageDraw, ImageFont
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont
from fontTools.pens.svgPathPen import SVGPathPen

ROOT = Path(__file__).resolve().parents[1]
FONT = ROOT / 'scripts/assets/fonts/space-grotesk/SpaceGrotesk.ttf'
INK, PAPER, LIME, RULE = '#202122', '#FAFAFA', '#EEFF41', '#5B5D60'


class Artwork:
    def __init__(self, width, height):
        self.width, self.height = width, height
        self.image = Image.new('RGB', (width, height), INK)
        self.draw = ImageDraw.Draw(self.image)
        self.svg = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}">',
                    '<title>Retrocycles League</title>',
                    f'<rect width="{width}" height="{height}" fill="{INK}"/>']

    def rect(self, x, y, width, height, fill):
        self.draw.rectangle((x, y, x+width-1, y+height-1), fill=fill)
        self.svg.append(f'<rect x="{x}" y="{y}" width="{width}" height="{height}" fill="{fill}"/>')

    def text(self, x, baseline, size, text, fill=PAPER, weight=600):
        font = ImageFont.truetype(str(FONT), size)
        font.set_variation_by_axes([weight])
        self.draw.text((x, baseline), text, font=font, fill=fill, anchor='ls')
        outlined = instantiateVariableFont(TTFont(FONT), {'wght': weight})
        glyphs, cmap = outlined.getGlyphSet(), outlined.getBestCmap()
        scale = size / outlined['head'].unitsPerEm
        self.svg.append(f'<g aria-label="{escape(text)}" fill="{fill}">')
        for char in text:
            glyph = glyphs[cmap[ord(char)]]
            pen = SVGPathPen(glyphs)
            glyph.draw(pen)
            self.svg.append(f'<path transform="translate({x:.4f} {baseline}) scale({scale:.6f} {-scale:.6f})" d="{pen.getCommands()}"/>')
            x += glyph.width * scale
        self.svg.append('</g>')

    def save(self, path):
        path.with_suffix('.svg').write_text('\n'.join(self.svg + ['</svg>', '']), encoding='utf-8')
        self.image.save(path.with_suffix('.png'))


def main():
    folder = ROOT / 'resources/brand'
    folder.mkdir(parents=True, exist_ok=True)
    icon = Artwork(1024, 1024)
    icon.rect(32, 32, 960, 4, RULE)
    icon.text(102, 648, 420, 'RCL')
    icon.rect(115, 747, 794, 45, LIME)
    icon.save(folder / 'rcl-icon')
    icon.image.resize((128, 128), Image.Resampling.LANCZOS).save(ROOT / 'textures/icon.png')
    icon.image.save(ROOT / 'tron.ico', sizes=[(s, s) for s in (16, 24, 32, 48, 64, 128, 256)])
    icon.image.save(folder / 'rcl.icns', format='ICNS')
    splash = Artwork(2048, 1024)
    splash.text(140, 225, 42, 'retrocycles league', weight=500)
    splash.text(128, 605, 330, 'RCL')
    splash.rect(146, 672, 650, 18, LIME)
    splash.text(145, 867, 32, 'competitive lightcycles', weight=400)
    # A single orthogonal trail: the game's geometry, outside the wordmark.
    for x, y, w, h in [(1160, 194, 720, 3), (1877, 194, 3, 570),
                        (1100, 761, 780, 3), (1097, 416, 3, 348),
                        (1097, 416, 487, 3), (1581, 416, 3, 210)]:
        splash.rect(x, y, w, h, RULE)
    splash.rect(1230, 307, 470, 8, LIME)
    splash.rect(1692, 307, 8, 295, LIME)
    splash.rect(1680, 590, 32, 32, PAPER)
    splash.save(folder / 'rcl-splash')
    splash.image.save(ROOT / 'textures/title.png')
    splash.image.save(ROOT / 'textures/title.jpg', quality=95, subsampling=0)
    print('Exported RCL SVG, PNG, ICO (16–256px), ICNS (1024px), and splash.')


if __name__ == '__main__':
    main()
