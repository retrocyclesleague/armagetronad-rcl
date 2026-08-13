#!/usr/bin/env python3
"""Rasterize the original vector font atlases at 1024 by 1024.

The game addresses fonts as a fixed 16 by 8 grid.  Rendering the repository's
path-based SVGs at four device pixels per SVG pixel preserves the established
Armagetronad letterforms while doubling the resolution of each atlas cell.
Set CHROME to a Chromium-family executable when it is not in a standard path.
"""

import os
from pathlib import Path
import shutil
import subprocess

from PIL import Image, ImageChops, ImageStat


ROOT = Path(__file__).resolve().parents[1]
ATLAS_SIZE = 1024
LEGACY_SIZE = 512


def find_chrome() -> str:
    configured = os.environ.get("CHROME")
    candidates = [
        configured,
        shutil.which("google-chrome"),
        shutil.which("chromium"),
        shutil.which("chromium-browser"),
        "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
        "/Applications/Chromium.app/Contents/MacOS/Chromium",
    ]
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return candidate
    raise SystemExit("Chromium not found; set CHROME to its executable path")


def rasterize(chrome: str, source: Path, output: Path) -> None:
    subprocess.run(
        [
            chrome,
            "--headless=new",
            "--disable-gpu",
            "--no-sandbox",
            "--hide-scrollbars",
            "--force-device-scale-factor=4",
            "--window-size=256,256",
            "--default-background-color=00000000",
            f"--screenshot={output}",
            source.as_uri(),
        ],
        check=True,
    )


def validate(output: Path, legacy: Path, primary: bool) -> None:
    rendered = Image.open(output).convert("RGBA")
    if rendered.size != (ATLAS_SIZE, ATLAS_SIZE):
        raise SystemExit(f"{output}: expected 1024x1024, got {rendered.size}")

    alpha = rendered.getchannel("A")
    if primary and alpha.crop((0, 0, ATLAS_SIZE, 2 * 128)).getbbox():
        raise SystemExit(f"{output}: control-character rows are not empty")

    # A four-times SVG raster should reduce to the checked-in 512 atlas with
    # only small antialiasing differences.  This catches viewport shifts that
    # would break the renderer's 16x8 cell addressing.
    reduced = alpha.resize((LEGACY_SIZE, LEGACY_SIZE), Image.Resampling.LANCZOS)
    reference = Image.open(legacy).convert("RGBA").getchannel("A")
    difference = ImageChops.difference(reduced, reference)
    mean_error = ImageStat.Stat(difference).mean[0]
    if mean_error >= 2.0:
        raise SystemExit(
            f"{output}: atlas geometry differs from {legacy} (MAE {mean_error:.3f})"
        )
    print(f"{output.relative_to(ROOT)}: 1024x1024, geometry MAE {mean_error:.3f}")


def main() -> None:
    chrome = find_chrome()
    jobs = [
        ("font.svg", "font_rcl.png", "font.png", True),
        ("font_extra.svg", "font_extra_rcl.png", "font_extra.png", False),
    ]
    for source_name, output_name, legacy_name, primary in jobs:
        source = ROOT / "textures" / source_name
        output = ROOT / "textures" / output_name
        legacy = ROOT / "textures" / legacy_name
        rasterize(chrome, source, output)
        validate(output, legacy, primary)


if __name__ == "__main__":
    main()
