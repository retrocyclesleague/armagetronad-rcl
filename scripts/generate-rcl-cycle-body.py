#!/usr/bin/env python3
"""Generate the RCL lightcycle body mesh and its texture.

The body is a smooth lofted shell: superellipse rings along the cycle's
length, following top, bottom and width profiles. It sits over the existing
wheel models, which the game places at (0, 0, .73) and (1.84, 0, .43) in the
body's coordinates.

The engine textures a .mod by projecting from the side (u from x, v from z)
and replaces transparent texels with the player's colour. The texture is
drawn in that same projection: coloured upper shell, pale lower fairing,
dark canopy glass and a headlight.

    python3 scripts/generate-rcl-cycle-body.py
"""

import math
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
RINGS = 40          # sections along the length
SEGMENTS = 28       # points around each section
ROUNDNESS = 2.6     # superellipse exponent; 2 is an ellipse
TEXTURE = 1024

# (x, value) control points, front of the cycle at +x
TOP = [(-0.62, 1.10), (-0.45, 1.36), (0.0, 1.53), (0.55, 1.58), (1.1, 1.43),
       (1.6, 1.15), (2.1, 0.88), (2.42, 0.64)]
BOTTOM = [(-0.62, 0.86), (-0.45, 0.64), (0.0, 0.44), (0.6, 0.27), (1.2, 0.27),
          (1.7, 0.35), (2.1, 0.45), (2.42, 0.55)]
WIDTH = [(-0.62, 0.07), (-0.45, 0.23), (0.0, 0.36), (0.6, 0.38), (1.2, 0.35),
         (1.84, 0.33), (2.2, 0.22), (2.42, 0.07)]
X0, X1 = TOP[0][0], TOP[-1][0]


def curve(points, x):
    """Catmull-Rom through the control points."""
    for i in range(len(points) - 1):
        if x <= points[i + 1][0] or i == len(points) - 2:
            p0 = points[max(i - 1, 0)][1]
            p1, p2 = points[i][1], points[i + 1][1]
            p3 = points[min(i + 2, len(points) - 1)][1]
            t = (x - points[i][0]) / (points[i + 1][0] - points[i][0])
            t = max(0.0, min(1.0, t))
            return 0.5 * ((2 * p1) + (-p0 + p2) * t
                          + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t
                          + (-p0 + 3 * p1 - 3 * p2 + p3) * t ** 3)
    return points[-1][1]


def power(value):
    return math.copysign(abs(value) ** (2.0 / ROUNDNESS), value)


def build_mesh():
    vertices, faces = [], []
    for i in range(RINGS):
        # sample the ends more densely, where the shell closes
        t = 0.5 - 0.5 * math.cos(math.pi * i / (RINGS - 1))
        x = X0 + (X1 - X0) * t
        top, bottom, width = curve(TOP, x), curve(BOTTOM, x), curve(WIDTH, x)
        centre, half = (top + bottom) / 2, (top - bottom) / 2
        for j in range(SEGMENTS):
            a = 2 * math.pi * j / SEGMENTS
            vertices.append((x, width * power(math.cos(a)),
                             centre + half * power(math.sin(a))))

    def ring(i, j):
        return i * SEGMENTS + (j % SEGMENTS)

    for i in range(RINGS - 1):
        for j in range(SEGMENTS):
            a, b = ring(i, j), ring(i + 1, j)
            c, d = ring(i + 1, j + 1), ring(i, j + 1)
            faces += [(a, b, c), (a, c, d)]

    # close both ends with a fan
    for i, x in ((0, X0), (RINGS - 1, X1)):
        centre = len(vertices)
        vertices.append((x, 0.0, (curve(TOP, x) + curve(BOTTOM, x)) / 2))
        for j in range(SEGMENTS):
            faces.append((centre, ring(i, j), ring(i, j + 1)))

    # The engine culls back faces: wind every triangle outwards, away from
    # the shell's centre line at that x.
    fixed = []
    for a, b, c in faces:
        pa, pb, pc = vertices[a], vertices[b], vertices[c]
        u = [pb[k] - pa[k] for k in range(3)]
        v = [pc[k] - pa[k] for k in range(3)]
        normal = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2],
                  u[0] * v[1] - u[1] * v[0])
        mid = [(pa[k] + pb[k] + pc[k]) / 3 for k in range(3)]
        axis_x = max(X0 + 0.05, min(X1 - 0.05, mid[0]))
        axis = (axis_x, 0.0, (curve(TOP, axis_x) + curve(BOTTOM, axis_x)) / 2)
        out = sum(normal[k] * (mid[k] - axis[k]) for k in range(3))
        fixed.append((a, b, c) if out >= 0 else (a, c, b))
    return vertices, fixed


def smooth(edge0, edge1, value):
    t = max(0.0, min(1.0, (value - edge0) / (edge1 - edge0)))
    return t * t * (3 - 2 * t)


def build_texture(vertices):
    xs = [v[0] for v in vertices]
    zs = [v[2] for v in vertices]
    xmin, xmax, zmin, zmax = min(xs), max(xs), min(zs), max(zs)
    image = Image.new("RGBA", (TEXTURE, TEXTURE))
    pixels = image.load()
    for py in range(TEXTURE):
        z = zmax - (py + 0.5) / TEXTURE * (zmax - zmin)
        for px in range(TEXTURE):
            x = xmin + (px + 0.5) / TEXTURE * (xmax - xmin)
            top, bottom = curve(TOP, x), curve(BOTTOM, x)
            height = (z - bottom) / max(top - bottom, 1e-3)

            # start as player colour (transparent), slightly shaded low down
            grey, alpha = 255.0, 0.0

            # pale lower fairing with a dark seam above it
            fairing = 1 - smooth(0.36, 0.39, height)
            grey, alpha = 236.0, max(alpha, fairing * 255)
            seam = smooth(0.375, 0.39, height) * (1 - smooth(0.40, 0.415, height))
            if seam > 0:
                grey = grey * (1 - seam) + 22 * seam
                alpha = max(alpha, seam * 255)

            # canopy glass
            glass = ((x - 0.70) / 0.62) ** 2 + ((z - (top - 0.02)) / 0.34) ** 2
            cover = 1 - smooth(0.92, 1.0, glass)
            if cover > 0:
                shine = 20 + 70 * smooth(0.55, 0.0, glass) * smooth(0.2, 1.0, height)
                grey = grey * (1 - cover) + shine * cover
                alpha = max(alpha, cover * 255)

            # headlight in the nose
            lamp = 1 - smooth(0.05, 0.085, math.hypot(x - 2.30, (z - 0.66) * 1.2))
            if lamp > 0:
                grey = grey * (1 - lamp) + 255 * lamp
                alpha = max(alpha, lamp * 255)

            value = int(round(grey))
            pixels[px, py] = (value, value, value, int(round(alpha)))
    return image


def main():
    vertices, faces = build_mesh()
    with open(ROOT / "models" / "cycle_body.mod", "w", newline="\n") as out:
        for index, (x, y, z) in enumerate(vertices, 1):
            out.write("v %d\t%.6f\t%.6f\t%.6f\n" % (index, x, y, z))
        for a, b, c in faces:
            out.write("f \t%d\t%d\t%d\n" % (a + 1, b + 1, c + 1))
    build_texture(vertices).save(ROOT / "textures" / "cycle_body.png")
    print("cycle body: %d vertices, %d faces" % (len(vertices), len(faces)))


if __name__ == "__main__":
    main()
