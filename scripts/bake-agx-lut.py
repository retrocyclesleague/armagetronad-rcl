"""Bake Blender's AgX view transform into shaders/agx.cube for the game.

The new renderer's tone map (shaders/tonemap.frag) looks AgX up in this
table, so the game's view transform is Blender's own and not an
approximation of it. Run with the Blender whose look the renders use:

  blender -b --factory-startup --python scripts/bake-agx-lut.py -- shaders/agx.cube

The table is a 3D LUT in the .cube layout (red fastest, then green, then
blue). Its input is not linear light but a log2 shaper of it, the range AgX
itself works in: t = (log2(light) - LOW) / (HIGH - LOW), clamped to 0..1, per
channel, on Linear Rec.709 light. Its output is display values for the sRGB
display, view AgX, look None, exactly as OCIO in Blender gives them. The
shaper numbers are written into the file's comments and read by nothing:
tonemap.frag has them as constants, so change both together.
"""

import os
import sys

import bpy
import numpy as np
import PyOpenColorIO as OCIO

SIZE = 33
LOW = -12.47393
HIGH = 4.026069


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    out = argv[0] if argv else "agx.cube"

    path = os.path.join(bpy.utils.resource_path("LOCAL"), "datafiles", "colormanagement", "config.ocio")
    config = OCIO.Config.CreateFromFile(path)
    source = config.getColorSpace("scene_linear").getName()
    transform = OCIO.DisplayViewTransform(src=source, display="sRGB", view="AgX")
    cpu = config.getProcessor(transform).getDefaultCPUProcessor()

    def agx(light):
        return np.array(cpu.applyRGB([float(light[0]), float(light[1]), float(light[2])]))

    def shaper_inverse(t):
        return np.power(2.0, LOW + np.asarray(t) * (HIGH - LOW))

    steps = np.linspace(0.0, 1.0, SIZE)
    table = np.zeros((SIZE, SIZE, SIZE, 3))  # [blue][green][red]
    for b in range(SIZE):
        for g in range(SIZE):
            for r in range(SIZE):
                table[b, g, r] = agx(shaper_inverse([steps[r], steps[g], steps[b]]))

    # how far the table, read the way the shader reads it, is from OCIO
    def lookup(light):
        t = np.clip((np.log2(np.maximum(light, 2.0 ** LOW)) - LOW) / (HIGH - LOW), 0, 1) * (SIZE - 1)
        i = np.minimum(np.floor(t).astype(int), SIZE - 2)
        f = t - i
        out = np.zeros(3)
        for db in (0, 1):
            for dg in (0, 1):
                for dr in (0, 1):
                    w = (f[2] if db else 1 - f[2]) * (f[1] if dg else 1 - f[1]) * (f[0] if dr else 1 - f[0])
                    out += w * table[i[2] + db, i[1] + dg, i[0] + dr]
        return out

    rng = np.random.default_rng(7)
    worst = 0.0
    errors = []
    for _ in range(4000):
        # light from deep shadow to well over white, saturated and not
        light = shaper_inverse(rng.uniform(0.25, 1.0, 3))
        e = np.abs(lookup(light) - agx(light)).max() * 255
        errors.append(e)
        worst = max(worst, e)
    for v in (0.0, 0.18, 1.0, 4.0):
        e = np.abs(lookup(np.array([v, v, v])) - agx([v, v, v])).max() * 255
        errors.append(e)
        worst = max(worst, e)

    with open(out, "w", newline="\n") as f:
        f.write("# Blender %s AgX view transform (display sRGB, view AgX, look None), from %s\n"
                % (bpy.app.version_string, source))
        f.write("# made by scripts/bake-agx-lut.py; read by the new renderer's tone map (rPost, tonemap.frag)\n")
        f.write("# input: per channel t = (log2(light) - %.6f) / (%.6f - %.6f), clamped to 0..1\n" % (LOW, HIGH, LOW))
        f.write("# trilinear read against OCIO, 4000 samples: largest %.2f, mean %.2f 8-bit steps\n"
                % (worst, float(np.mean(errors))))
        f.write('TITLE "RCL AgX"\n')
        f.write("LUT_3D_SIZE %d\n" % SIZE)
        f.write("DOMAIN_MIN 0.0 0.0 0.0\n")
        f.write("DOMAIN_MAX 1.0 1.0 1.0\n")
        for b in range(SIZE):
            for g in range(SIZE):
                for r in range(SIZE):
                    c = np.clip(table[b, g, r], 0, 1)
                    f.write("%.6f %.6f %.6f\n" % (c[0], c[1], c[2]))

    print("wrote", out, "size", SIZE, "largest error %.2f mean %.2f (8-bit steps)" % (worst, float(np.mean(errors))))


main()
