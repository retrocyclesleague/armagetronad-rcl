#!/usr/bin/env python3
"""Puts the light cycle's asset pack where the game reads it.

  python scripts/install-cycle-assets.py <export folder, e.g. rcl-cfg/art/lightcycle/export/D2>

Writes, under the source tree's data folders:
  models/rcl/lightcycle.rclmesh              the levels of detail (scripts/convert-cycle-mesh.py),
                                             from the .glb files when there are, else the .obj
  textures/rcl/lightcycle_lod<N>_<map>.png   the baked maps of each level (each has its own UVs):
                                             normal, basecolor, ao, roughness, mask
  textures/rcl/env_<look>_spec_m<N>.hdr      the arena looks' prefiltered cubemaps (pale, night)
  textures/rcl/env_<look>_irradiance.hdr     and their diffuse irradiance
  textures/rcl/brdf_lut_ggx.png              the split-sum table

Whatever the pack does not have yet is left out; the game draws without it (gCycleMesh.cpp says
how). So are a level's maps when its material id map shows they were baked on other UVs than
the mesh has now. Files of an older pack that this one lacks are removed, so the folder holds
one pack.
"""
import glob
import os
import shutil
import struct
import subprocess
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
MAPS = ["normal", "basecolor", "ao", "roughness", "mask"]
# the material id map's colours (8-bit), by the mesh's material order
MATID = [(128, 128, 128), (0, 64, 255), (255, 255, 0), (255, 0, 0), (0, 200, 0), (255, 0, 255)]


def read_png_rgb(path):
    """An 8-bit, non-interlaced PNG as (width, height, rows of RGB bytes), top row first."""
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    at, idat, head = 8, b"", None
    while at < len(data):
        length, kind = struct.unpack(">I4s", data[at:at + 8])
        body = data[at + 8:at + 8 + length]
        if kind == b"IHDR":
            head = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat += body
        at += 12 + length
    width, height, depth, colour, _c, _f, interlace = head
    channels = {0: 1, 2: 3, 4: 2, 6: 4}.get(colour)
    if depth != 8 or interlace or channels is None:
        return None
    raw = zlib.decompress(idat)
    stride = width * channels
    rows, prior = [], bytearray(stride)
    for y in range(height):
        kind = raw[y * (stride + 1)]
        row = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = row[x - channels] if x >= channels else 0
            b = prior[x]
            c = prior[x - channels] if x >= channels else 0
            if kind == 1:
                row[x] = (row[x] + a) & 255
            elif kind == 2:
                row[x] = (row[x] + b) & 255
            elif kind == 3:
                row[x] = (row[x] + (a + b) // 2) & 255
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                row[x] = (row[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(row) if channels == 3 else bytes(v for i in range(width)
                                                          for v in (row[i * channels:i * channels + 3]
                                                                    if channels >= 3 else row[i * channels:i * channels + 1] * 3)))
        prior = row
    return width, height, rows


def maps_fit(mesh_path, level, matid_path):
    """The share of the level's triangles whose middle lands on its own material in the id map.

    The maps are top row first and the mesh's v runs up (Blender's way), as the game reads them."""
    image = read_png_rgb(matid_path)
    if image is None:
        return None
    width, height, rows = image
    d = open(mesh_path, "rb").read()
    at = 84
    for l in range(level + 1):
        vc, ic, rc = struct.unpack_from("<III", d, at)
        at += 12
        ranges = [struct.unpack_from("<BBHII", d, at + 12 * r) for r in range(rc)]
        at += 12 * rc
        verts_at = at
        at += vc * 48
        idx_at = at
        at += ic * 4
    fits = total = 0
    for _o, material, _p, first, count in ranges:
        for t in range(first, first + count, 3 * max(1, count // 300)):
            u = v = 0.0
            for k in range(3):
                i = struct.unpack_from("<I", d, idx_at + 4 * (t + k))[0]
                u += struct.unpack_from("<f", d, verts_at + 48 * i + 40)[0] / 3
                v += struct.unpack_from("<f", d, verts_at + 48 * i + 44)[0] / 3
            x = min(width - 1, max(0, int(u * width)))
            y = min(height - 1, max(0, int((1 - v) * height)))
            colour = rows[y][3 * x:3 * x + 3]
            fits += sum(abs(a - b) for a, b in zip(colour, MATID[material])) < 60
            total += 1
    return fits / total if total else None


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    pack = os.path.abspath(sys.argv[1])
    models = os.path.join(ROOT, "models", "rcl")
    textures = os.path.join(ROOT, "textures", "rcl")
    os.makedirs(models, exist_ok=True)
    os.makedirs(textures, exist_ok=True)

    # the meshes: lod0 first, glTF (with Blender's tangents) where every level has one
    lods = sorted(glob.glob(os.path.join(pack, "*_lod[0-9].glb")))
    if not lods:
        lods = sorted(glob.glob(os.path.join(pack, "*_lod[0-9].obj")))
    if not lods:
        raise SystemExit("no *_lod<N>.glb or .obj in %s" % pack)
    command = [sys.executable, os.path.join(HERE, "convert-cycle-mesh.py")] + lods + \
              ["-o", os.path.join(models, "lightcycle.rclmesh")]
    report = os.path.join(pack, "build_report.json")
    if os.path.exists(report):
        command += ["--report", report]
    subprocess.check_call(command)

    wanted = set()

    def put(source, name):
        shutil.copyfile(source, os.path.join(textures, name))
        wanted.add(name)
        print("copied %s -> textures/rcl/%s" % (os.path.basename(source), name))

    # lod0's maps carry no level in their names; the others do
    def pack_map(lod, kind):
        found = glob.glob(os.path.join(pack, "*_%s.png" % kind)) if lod == 0 else \
            glob.glob(os.path.join(pack, "*_lod%d_%s.png" % (lod, kind)))
        found = [f for f in found if lod > 0 or "_lod" not in os.path.basename(f)]
        return found[0] if found else None

    mesh = os.path.join(models, "lightcycle.rclmesh")
    for lod in range(len(lods)):
        # A level's maps must have been baked on its UVs as they are now: a
        # bake older than the mesh puts lights and metal in the wrong places.
        # Without them the game draws that level with its materials' own
        # colours, which is right if plainer.
        matid = pack_map(lod, "matid")
        fit = maps_fit(mesh, lod, matid) if matid else None
        if fit is not None and fit < 0.75:
            print("lod%d: maps left out, they do not fit its UVs (%.0f%% of triangles on their material; "
                  "bake them again on this mesh)" % (lod, fit * 100))
            continue
        print("lod%d: maps fit its UVs (%s)" % (lod, "%.0f%%" % (fit * 100) if fit is not None else "no id map to check"))
        for m in MAPS:
            found = pack_map(lod, m)
            if found:
                put(found, "lightcycle_lod%d_%s.png" % (lod, m))

    for f in sorted(glob.glob(os.path.join(pack, "env_*_spec_m*.hdr")) +
                    glob.glob(os.path.join(pack, "env_*_irradiance.hdr")) +
                    glob.glob(os.path.join(pack, "brdf_lut_ggx.png"))):
        put(f, os.path.basename(f))

    for f in os.listdir(textures):
        if f not in wanted:
            os.remove(os.path.join(textures, f))
            print("removed textures/rcl/%s (not in this pack)" % f)


if __name__ == "__main__":
    main()
