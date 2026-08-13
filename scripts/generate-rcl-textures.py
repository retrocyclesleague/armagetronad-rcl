#!/usr/bin/env python3
"""Generate the RCL high-definition texture set.

The runtime filenames deliberately stay identical to the classic client.  That
keeps moviepacks, UV coordinates, server-selected colours, and the old fixed
function renderer working exactly as before while replacing the tiny source
images with deterministic, power-of-two assets.

No downloaded material is used.  World textures are procedural and the cycle
body is an edge-preserving remaster of the GPL classic texture captured under
``scripts/assets/rcl-textures``.  Run once with ``--capture-classic`` before
the first generation in a fresh checkout.
"""

from __future__ import annotations

import argparse
import hashlib
import math
import shutil
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageEnhance, ImageFilter, ImageFont
from PIL.PngImagePlugin import PngInfo


VERSION = "rcl-hd-textures-v1"
ROOT = Path(__file__).resolve().parents[1]
TEXTURES = ROOT / "textures"
CLASSIC = ROOT / "scripts" / "assets" / "rcl-textures"

CLASSIC_SHA256 = {
    "floor.png": "62813704eabd831db31db1bbd5f4005334e838649858271877654bdc9c6c13a5",
    "floor_a.png": "f0c4c6a9cdec657152d0a96c7703c9127aa4be33631366ed94dcbcad0be621c4",
    "floor_b.png": "abe35541a451ed3f0603d103dd616105ec1c25f28792e63eeef944fe9e794e5b",
    "rim_wall.png": "83d14bb35f0a2445f6c1124b8374fdfb5f8e58ef71af04487fd28ef938e75918",
    "dir_wall.png": "99166c0f5a29c0098303a4b7096edbbb339662e80d2892660c1bd4497bc0dfe9",
    "cycle_body.png": "4c79674a77713afe613befe4890852d75ce9f4e6ab50268e0d665e367d79e420",
    "cycle_wheel.png": "fce47455fc91b7750edc30aa4a94942b82803632df4c3e13026baeede6e70442",
}

OUTPUT_SPECS = {
    "floor.png": ((1024, 1024), "L", True, True),
    "floor_a.png": ((16, 1024), "L", True, True),
    "floor_b.png": ((1024, 16), "L", True, True),
    "rim_wall.png": ((1024, 1024), "RGB", True, True),
    "dir_wall.png": ((1024, 1024), "L", True, False),
    "cycle_body.png": ((2048, 2048), "LA", False, False),
    "cycle_wheel.png": ((1024, 1024), "LA", False, False),
}

GENERATED_PIXEL_SHA256 = {
    "floor.png": "1ab1b629d3d6b0cf2fddd10c563f943103afed6cdb138fc4422350b3e5c46eda",
    "floor_a.png": "c778dead86c63d0050be4b1b333e7ed386697698d61a577aa9e48fa6223930ae",
    "floor_b.png": "9821751d735d7de656aa8c12f893a2bf7e00158e2f8caa63410feaeba6625cf3",
    "rim_wall.png": "fdeb8ba8b9f4cf7f3efb8c9dc5c1d78ff52869a09257018c0ed0c851650ac824",
    "dir_wall.png": "a636346fcd4048b4e93389a1605339aa768cc68f940cd0c75671b6bc7818b334",
    "cycle_body.png": "b63c842ea1ee18134c9d21ea7a28db4cd2f9a2f00b931f2ae1a54ae6b8332a2f",
    "cycle_wheel.png": "c36222ecfbd4bc860c76591480225941218ac71b6fd318ebf8ab5c38f705d4e2",
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def pixel_sha256(image: Image.Image) -> str:
    """Hash decoded pixels, independent of PNG encoder/compression version."""
    digest = hashlib.sha256()
    digest.update(image.mode.encode("ascii"))
    digest.update(f"{image.width}x{image.height}".encode("ascii"))
    digest.update(image.tobytes())
    return digest.hexdigest()


def metadata(description: str) -> PngInfo:
    info = PngInfo()
    info.add_text("Software", f"Retrocycles League {VERSION}")
    info.add_text("Description", description)
    info.add_text("License", "GPL-2.0-or-later; generated in the Armagetron Advanced source tree")
    return info


def save(image: Image.Image, name: str, description: str) -> None:
    image.save(
        TEXTURES / name,
        format="PNG",
        pnginfo=metadata(description),
        optimize=True,
        compress_level=9,
    )


def capture_classic() -> None:
    CLASSIC.mkdir(parents=True, exist_ok=True)
    for name, expected in CLASSIC_SHA256.items():
        source = TEXTURES / name
        destination = CLASSIC / name
        if destination.exists():
            if sha256(destination) != expected:
                raise RuntimeError(f"classic reference has unexpected content: {destination}")
            continue
        actual = sha256(source)
        if actual != expected:
            raise RuntimeError(
                f"refusing to capture modified {source}; expected {expected}, got {actual}"
            )
        shutil.copyfile(source, destination)
        print(f"captured {destination.relative_to(ROOT)}")


def periodic_edges(image: Image.Image, repeat_x: bool, repeat_y: bool) -> None:
    """Make first/last texels identical for bilinear GL_REPEAT sampling."""
    pixels = image.load()
    width, height = image.size
    if repeat_x:
        for y in range(height):
            pixels[width - 1, y] = pixels[0, y]
    if repeat_y:
        for x in range(width):
            pixels[x, height - 1] = pixels[x, 0]


def generate_floor() -> None:
    size = 1024
    image = Image.new("L", (size, size), 0)
    pixels = image.load()

    # Dark graphite whose low-frequency component is exactly periodic.  The
    # tiny integer hash avoids a sterile flat surface without introducing a
    # random seed or visible tiling seams.
    period = size - 1
    for y in range(size):
        py = y % period
        for x in range(size):
            px = x % period
            wave = 3.0 * math.sin(2.0 * math.pi * px / period)
            wave += 2.0 * math.cos(2.0 * math.pi * py / period)
            grain = ((px * 17 + py * 31 + (px ^ py) * 7) % 7) - 3
            pixels[x, y] = max(0, min(255, int(45 + wave + grain)))

    draw = ImageDraw.Draw(image)

    # One octagonal arena panel per gameplay grid tile.  Highlights and
    # shadows are deliberately restrained so trails remain the visual focus.
    outer = [(150, 66), (874, 66), (958, 150), (958, 874),
             (874, 958), (150, 958), (66, 874), (66, 150)]
    inner = [(174, 94), (850, 94), (930, 174), (930, 850),
             (850, 930), (174, 930), (94, 850), (94, 174)]
    draw.line(outer + [outer[0]], fill=74, width=8, joint="curve")
    draw.line(inner + [inner[0]], fill=29, width=7, joint="curve")
    draw.line([(151, 74), (870, 74), (948, 152)], fill=92, width=3)
    draw.line([(76, 870), (152, 948), (870, 948)], fill=24, width=3)

    # Circuit breaks and inspection plates echo the classic technical grid
    # without adding texture edges that could be mistaken for cycle walls.
    traces = [
        [(130, 334), (260, 334), (304, 290), (472, 290)],
        [(894, 684), (764, 684), (720, 728), (552, 728)],
        [(346, 130), (346, 218), (390, 262)],
        [(678, 894), (678, 806), (634, 762)],
    ]
    for points in traces:
        draw.line(points, fill=67, width=4, joint="curve")
        draw.line([(x + 3, y + 3) for x, y in points], fill=33, width=2, joint="curve")

    for x, y in [(472, 290), (552, 728), (390, 262), (634, 762)]:
        draw.rounded_rectangle((x - 12, y - 12, x + 12, y + 12), radius=4, fill=32, outline=82, width=3)
        draw.rectangle((x - 3, y - 3, x + 3, y + 3), fill=104)

    # A narrow, clean grid boundary replaces the old 1-pixel border.
    draw.rectangle((0, 0, size - 1, size - 1), outline=88, width=5)
    draw.rectangle((6, 6, size - 7, size - 7), outline=24, width=3)
    periodic_edges(image, True, True)
    save(image, "floor.png", "RCL HD procedural graphite arena floor; seamless grayscale tint mask")

    # The two-texture floor path stretches these orthogonal strips in one
    # direction.  A smooth profile keeps the grid crisp under mipmapping.
    profile = []
    for coordinate in range(size):
        distance = min(coordinate, size - 1 - coordinate)
        highlight = 215.0 * math.exp(-((distance / 5.5) ** 2))
        halo = 42.0 * math.exp(-((distance / 18.0) ** 2))
        profile.append(max(0, min(255, int(2 + highlight + halo))))

    floor_a = Image.new("L", (16, size), 0)
    floor_a_pixels = floor_a.load()
    for y, value in enumerate(profile):
        for x in range(16):
            floor_a_pixels[x, y] = value
    periodic_edges(floor_a, True, True)
    save(floor_a, "floor_a.png", "RCL HD seamless vertical floor-grid luminance strip")

    floor_b = Image.new("L", (size, 16), 0)
    floor_b_pixels = floor_b.load()
    for x, value in enumerate(profile):
        for y in range(16):
            floor_b_pixels[x, y] = value
    periodic_edges(floor_b, True, True)
    save(floor_b, "floor_b.png", "RCL HD seamless horizontal floor-grid luminance strip")


def glow_lines(base: Image.Image, lines: list[tuple[list[tuple[int, int]], tuple[int, int, int], int]]) -> None:
    glow = Image.new("RGBA", base.size, (0, 0, 0, 0))
    glow_draw = ImageDraw.Draw(glow)
    core = Image.new("RGBA", base.size, (0, 0, 0, 0))
    core_draw = ImageDraw.Draw(core)
    for points, colour, width in lines:
        glow_draw.line(points, fill=(*colour, 150), width=width * 5, joint="curve")
        core_draw.line(points, fill=(*colour, 235), width=width, joint="curve")
    glow = glow.filter(ImageFilter.GaussianBlur(radius=12))
    base.alpha_composite(glow)
    base.alpha_composite(core)


def generate_rim_wall() -> None:
    size = 1024
    image = Image.new("RGBA", (size, size), (16, 22, 29, 255))
    pixels = image.load()
    period = size - 1
    for y in range(size):
        py = y % period
        vertical = 8.0 * math.sin(math.pi * py / period) ** 2
        for x in range(size):
            px = x % period
            grain = ((px * 13 + py * 29 + (px ^ (py * 3)) * 5) % 9) - 4
            pixels[x, y] = (
                max(0, min(255, int(17 + vertical + grain * 0.45))),
                max(0, min(255, int(24 + vertical + grain * 0.55))),
                max(0, min(255, int(32 + vertical + grain * 0.65))),
                255,
            )

    draw = ImageDraw.Draw(image)
    # Bevelled wall modules; all marks stop short of the repeat boundary.
    draw.rounded_rectangle((58, 58, 966, 966), radius=42, outline=(61, 75, 86, 255), width=8)
    draw.rounded_rectangle((76, 76, 948, 948), radius=32, outline=(7, 11, 16, 255), width=5)
    draw.line([(92, 248), (274, 248), (326, 196), (512, 196)], fill=(48, 62, 73, 255), width=7)
    draw.line([(932, 776), (750, 776), (698, 828), (512, 828)], fill=(48, 62, 73, 255), width=7)
    draw.line([(92, 760), (240, 760), (288, 808), (392, 808)], fill=(8, 12, 17, 255), width=8)
    draw.line([(932, 264), (784, 264), (736, 216), (632, 216)], fill=(8, 12, 17, 255), width=8)

    cyan = (46, 220, 245)
    amber = (244, 184, 54)
    glow_lines(
        image,
        [
            ([(78, 116), (310, 116), (354, 160)], cyan, 4),
            ([(946, 908), (714, 908), (670, 864)], cyan, 4),
            ([(78, 908), (210, 908)], amber, 3),
            ([(946, 116), (814, 116)], amber, 3),
        ],
    )

    # High-resolution RCL mark rendered from the GPL font already shipped by
    # the game; this is real league branding, not fake live/status content.
    font_path = TEXTURES / "Armagetronad.ttf"
    font = ImageFont.truetype(str(font_path), 230)
    small_font = ImageFont.truetype(str(font_path), 54)
    mark = "RCL"
    bounds = draw.textbbox((0, 0), mark, font=font, stroke_width=2)
    mark_width = bounds[2] - bounds[0]
    draw.text(
        ((size - mark_width) / 2, 346),
        mark,
        font=font,
        fill=(210, 224, 231, 255),
        stroke_width=4,
        stroke_fill=(4, 8, 12, 255),
    )
    subtitle = "RETROCYCLES LEAGUE"
    subtitle_bounds = draw.textbbox((0, 0), subtitle, font=small_font)
    subtitle_width = subtitle_bounds[2] - subtitle_bounds[0]
    draw.text(((size - subtitle_width) / 2, 610), subtitle, font=small_font, fill=(91, 126, 139, 255))

    # Diagnostic rail, drawn symmetrically so optional vertical wrap is clean.
    for y in (24, size - 25):
        draw.line((0, y, size - 1, y), fill=(88, 103, 111, 255), width=2)
        for x in range(96, 929, 64):
            draw.rectangle((x, y - 5, x + 28, y + 5), fill=(35, 142, 158, 255))

    image = image.convert("RGB")
    periodic_edges(image, True, True)
    save(image, "rim_wall.png", "RCL HD procedural arena rim wall; seamless league-branded material")


def generate_direction_wall() -> None:
    size = 1024
    image = Image.new("L", (size, size), 0)
    pixels = image.load()
    period = size - 1
    for y in range(size):
        py = y % period
        edge_light = 24.0 * (math.exp(-((py / 34.0) ** 2)) + math.exp(-(((period - py) / 34.0) ** 2)))
        center = 13.0 * math.exp(-(((py - period / 2.0) / 300.0) ** 2))
        for x in range(size):
            px = x % period
            scan = 3 if py % 32 < 2 else 0
            grain = ((px * 11 + py * 23) % 5) - 2
            pixels[x, y] = max(0, min(255, int(86 + edge_light + center + scan + grain)))

    draw = ImageDraw.Draw(image)
    # Classic lightning/kink motif redrawn as a smooth, high-resolution energy
    # trace.  It modulates the player's trail colour exactly as before.
    path = [(116, 0), (392, 340), (330, 476), (514, 444), (630, 612), (908, 1023)]
    shadow_path = [(x + 7, y) for x, y in path]
    draw.line(shadow_path, fill=40, width=34, joint="curve")
    draw.line(path, fill=178, width=38, joint="curve")
    draw.line(path, fill=246, width=13, joint="curve")
    draw.line(path, fill=255, width=4, joint="curve")

    # Fine inner rails give the wall readable structure up close without
    # changing collision dimensions or geometry.
    for y in (52, 972):
        draw.line((0, y, size - 1, y), fill=145, width=4)
        draw.line((0, y + (8 if y < size / 2 else -8), size - 1, y + (8 if y < size / 2 else -8)), fill=58, width=3)

    periodic_edges(image, True, False)
    save(image, "dir_wall.png", "RCL HD direction-wall luminance material; classic energy-kink motif")


def generate_cycle_body() -> None:
    reference = CLASSIC / "cycle_body.png"
    if not reference.exists():
        raise RuntimeError("classic cycle reference missing; run with --capture-classic first")

    source = Image.open(reference).convert("LA")
    # Preserve the exact classic paint layout/player-colour mask.  Lanczos plus
    # restrained sharpening removes the old 256px stair-stepping, while the
    # high-frequency brushed finish only affects opaque metal/glass regions.
    remaster = source.resize((2048, 2048), Image.Resampling.LANCZOS)
    luminance, alpha = remaster.split()
    luminance = luminance.filter(ImageFilter.UnsharpMask(radius=2.2, percent=165, threshold=3))
    alpha = alpha.filter(ImageFilter.UnsharpMask(radius=1.6, percent=120, threshold=4))

    brushed = Image.new("L", luminance.size, 128)
    brushed_pixels = brushed.load()
    for y in range(brushed.height):
        band = int(5 * math.sin(y * 0.19) + 3 * math.sin(y * 0.047))
        for x in range(brushed.width):
            brushed_pixels[x, y] = max(0, min(255, 128 + band + ((x * 7 + y * 3) % 5) - 2))
    brushed = brushed.filter(ImageFilter.GaussianBlur(radius=0.45))
    finish = ImageEnhance.Contrast(brushed).enhance(0.55)
    finish = ImageChops.multiply(luminance, finish)
    finish = ImageEnhance.Brightness(finish).enhance(2.0)

    # Blend finish according to original opacity: player-colour sections stay
    # player-colour sections, preserving the recognisable classic bike.
    metal_mask = alpha.point(lambda value: max(0, min(96, (value - 96) * 2)))
    luminance = Image.composite(finish, luminance, metal_mask)
    body = Image.merge("LA", (luminance, alpha))
    save(body, "cycle_body.png", "RCL HD edge-preserving remaster of the classic cycle paint mask")


def generate_cycle_wheel() -> None:
    size = 1024
    luminance = Image.new("L", (size, size), 0)
    alpha = Image.new("L", (size, size), 0)
    lum_pixels = luminance.load()
    alpha_pixels = alpha.load()
    center = (size - 1) / 2.0

    for y in range(size):
        dy = (y - center) / center
        for x in range(size):
            dx = (x - center) / center
            radius = math.sqrt(dx * dx + dy * dy)
            angle = math.atan2(dy, dx)
            specular = max(0.0, math.cos(angle + 0.8)) ** 10

            if radius >= 0.64:
                # The classic wheel texture is player colour outside the
                # energy disc; keep that instantly recognisable proportion.
                lum, opacity = 236, 0
            elif radius >= 0.52:
                # Broad luminous player-colour aura around the dark disc.
                edge = (0.64 - radius) / 0.12
                lum = int(238 + 16 * specular)
                opacity = int(105 * math.sin(edge * math.pi))
            elif radius >= 0.43:
                # Narrow white/player-colour energized rim.
                edge = (radius - 0.43) / 0.09
                lum = int(218 + 30 * specular)
                opacity = int(150 + 70 * math.sin(edge * math.pi))
            elif radius >= 0.19:
                # Smooth black ceramic tyre: no automotive tread or spokes.
                band = 7 * math.exp(-(((radius - 0.34) / 0.025) ** 2))
                lum = int(7 + band + 25 * specular)
                opacity = 255
            elif radius >= 0.105:
                lum = int(218 + 28 * specular)
                opacity = 255
            else:
                lum = int(238 + 17 * specular)
                opacity = 255

            lum_pixels[x, y] = max(0, min(255, lum))
            alpha_pixels[x, y] = max(0, min(255, opacity))

    wheel = Image.merge("LA", (luminance, alpha))
    save(wheel, "cycle_wheel.png", "RCL HD procedural cycle wheel; classic player-colour mask semantics")


def edges_equal(image: Image.Image, horizontal: bool) -> bool:
    width, height = image.size
    if horizontal:
        return image.crop((0, 0, 1, height)).tobytes() == image.crop(
            (width - 1, 0, width, height)
        ).tobytes()
    return image.crop((0, 0, width, 1)).tobytes() == image.crop(
        (0, height - 1, width, height)
    ).tobytes()


def verify() -> None:
    failed = False
    for name, (expected_size, expected_mode, repeat_x, repeat_y) in OUTPUT_SPECS.items():
        path = TEXTURES / name
        with Image.open(path) as image:
            reasons = []
            if image.size != expected_size:
                reasons.append(f"size {image.size} != {expected_size}")
            if image.mode != expected_mode:
                reasons.append(f"mode {image.mode} != {expected_mode}")
            if repeat_x and not edges_equal(image, True):
                reasons.append("left/right seam mismatch")
            if repeat_y and not edges_equal(image, False):
                reasons.append("top/bottom seam mismatch")
            actual_hash = pixel_sha256(image)
            if actual_hash != GENERATED_PIXEL_SHA256[name]:
                reasons.append(
                    f"pixel hash {actual_hash} != {GENERATED_PIXEL_SHA256[name]}"
                )
            if reasons:
                failed = True
                print(f"FAIL {name}: {', '.join(reasons)}", file=sys.stderr)
            else:
                print(f"OK   {name}: {image.size[0]}x{image.size[1]} {image.mode} {actual_hash[:12]}")
    if failed:
        raise SystemExit(1)


def generate() -> None:
    generate_floor()
    generate_rim_wall()
    generate_direction_wall()
    generate_cycle_body()
    generate_cycle_wheel()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture-classic", action="store_true", help="capture and verify the original 0.2.x inputs")
    parser.add_argument("--verify-only", action="store_true", help="validate existing generated assets without rewriting them")
    args = parser.parse_args()

    if args.capture_classic:
        capture_classic()
    if not args.verify_only:
        generate()
    verify()


if __name__ == "__main__":
    main()
