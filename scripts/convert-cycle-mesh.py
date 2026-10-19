#!/usr/bin/env python3
"""Converts the light cycle's Blender export into one small binary file the client loads.

Input: the levels of detail, best as glTF binaries (lightcycle_*_lod0.glb ...) because they
carry Blender's MikkTSpace tangents, which the baked normal map was made against. OBJ works
too (tangents are then worked out here, close but not exact).

Output (.rclmesh, little-endian):
    char[8]  "RCLMESH1"
    u32      lod count
    f32[6]   bounds min xyz, max xyz (game axes: +x forward, +z up, wheels on z = 0)
    3 x      f32[4] pivot xyz and radius for body, front_wheel, rear_wheel
    per lod:
        u32  vertex count, u32 index count, u32 range count
        per range: u8 object, u8 material, u16 0, u32 first index, u32 index count
        vertices: f32 position[3], normal[3], tangent[4] (w = bitangent sign), uv[2]
        indices: u32
Wheel vertices are stored around their own axle so the game can turn them in place.
A converter, not a runtime parser: the game reads this file with one fread per block.
"""
import argparse
import json
import math
import os
import struct
import sys

OBJECTS = ["body", "front_wheel", "rear_wheel"]
MATERIALS = ["shell", "canopy", "light", "detail", "hub", "tyre"]
MAGIC = b"RCLMESH1"


# ---------------------------------------------------------------- glTF

def _accessor(gltf, binary, index):
    acc = gltf["accessors"][index]
    view = gltf["bufferViews"][acc["bufferView"]]
    comps = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[acc["type"]]
    fmt = {5126: "f", 5125: "I", 5123: "H", 5121: "B"}[acc["componentType"]]
    size = struct.calcsize("<" + fmt)
    stride = view.get("byteStride", size * comps)
    start = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    out = []
    for i in range(acc["count"]):
        o = start + i * stride
        vals = struct.unpack_from("<%d%s" % (comps, fmt), binary, o)
        out.append(vals if comps > 1 else vals[0])
    return out


def _quat_matrix(q):
    x, y, z, w = q
    return [
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ]


def _mat_vec(m, v):
    return tuple(m[r][0] * v[0] + m[r][1] * v[1] + m[r][2] * v[2] for r in range(3))


def read_glb(path, yup):
    data = open(path, "rb").read()
    magic, _version, _length = struct.unpack_from("<4sII", data, 0)
    if magic != b"glTF":
        raise SystemExit("%s: not a glTF binary" % path)
    off = 12
    gltf, binary = None, b""
    while off < len(data):
        clen, ctype = struct.unpack_from("<II", data, off)
        chunk = data[off + 8: off + 8 + clen]
        if ctype == 0x4E4F534A:
            gltf = json.loads(chunk.decode("utf-8"))
        elif ctype == 0x004E4942:
            binary = chunk
        off += 8 + clen
    mats = [m.get("name", "") for m in gltf.get("materials", [])]

    # node transforms (flattened; the export has no deep hierarchy, but follow parents anyway)
    parent = {}
    for i, n in enumerate(gltf["nodes"]):
        for c in n.get("children", []):
            parent[c] = i

    def world(i):
        n = gltf["nodes"][i]
        if "matrix" in n:
            raise SystemExit("%s: node %s uses a matrix; export with TRS" % (path, n.get("name")))
        rot = _quat_matrix(n.get("rotation", [0, 0, 0, 1]))
        s = n.get("scale", [1, 1, 1])
        t = n.get("translation", [0, 0, 0])
        m = [[rot[r][c] * s[c] for c in range(3)] for r in range(3)]
        if i in parent:
            pm, pt = world(parent[i])
            m = [[sum(pm[r][k] * m[k][c] for k in range(3)) for c in range(3)] for r in range(3)]
            t = [a + b for a, b in zip(_mat_vec(pm, t), pt)]
        return m, list(t)

    def to_game(v):
        # glTF is +y up; Blender's default export turned +z up into +y
        return (v[0], -v[2], v[1]) if yup else tuple(v)

    parts = {}
    for i, n in enumerate(gltf["nodes"]):
        if "mesh" not in n:
            continue
        name = n.get("name", "")
        obj = next((o for o in OBJECTS if name == o or name.startswith(o + ".")), None)
        if obj is None:
            raise SystemExit("%s: mesh node %r is not one of %s" % (path, name, OBJECTS))
        m, t = world(i)
        for prim in gltf["meshes"][n["mesh"]]["primitives"]:
            a = prim["attributes"]
            for need in ("POSITION", "NORMAL", "TEXCOORD_0", "TANGENT"):
                if need not in a:
                    raise SystemExit("%s: %s lacks %s (export tangents and UVs)" % (path, name, need))
            mat = mats[prim["material"]] if "material" in prim else ""
            mat = next((k for k in MATERIALS if mat == k or mat.startswith(k + ".")), None)
            if mat is None:
                raise SystemExit("%s: %s uses an unknown material" % (path, name))
            pos = [to_game([a_ + b for a_, b in zip(_mat_vec(m, p), t)]) for p in _accessor(gltf, binary, a["POSITION"])]
            nrm = [to_game(_mat_vec(m, p)) for p in _accessor(gltf, binary, a["NORMAL"])]
            tan = [to_game(_mat_vec(m, p[:3])) + (p[3],) for p in _accessor(gltf, binary, a["TANGENT"])]
            uv = [(u, 1.0 - v) for u, v in _accessor(gltf, binary, a["TEXCOORD_0"])]  # glTF v runs down
            idx = _accessor(gltf, binary, prim["indices"])
            parts.setdefault((obj, mat), []).append((pos, nrm, tan, uv, idx))
    return parts


# ---------------------------------------------------------------- OBJ

def read_obj(path):
    vs, vns, vts = [], [], []
    obj, mat = "body", "shell"
    faces = {}
    for line in open(path, encoding="utf-8"):
        w = line.split()
        if not w:
            continue
        if w[0] == "v":
            vs.append(tuple(map(float, w[1:4])))
        elif w[0] == "vn":
            vns.append(tuple(map(float, w[1:4])))
        elif w[0] == "vt":
            vts.append((float(w[1]), float(w[2])))
        elif w[0] == "o":
            obj = next((o for o in OBJECTS if w[1] == o or w[1].startswith(o + ".")), None)
            if obj is None:
                raise SystemExit("%s: object %r is not one of %s" % (path, w[1], OBJECTS))
        elif w[0] == "usemtl":
            mat = next((k for k in MATERIALS if w[1] == k or w[1].startswith(k + ".")), None)
            if mat is None:
                raise SystemExit("%s: unknown material %r" % (path, w[1]))
        elif w[0] == "f":
            corners = []
            for c in w[1:]:
                p = c.split("/")
                corners.append((int(p[0]) - 1, int(p[1]) - 1 if len(p) > 1 and p[1] else -1,
                                int(p[2]) - 1 if len(p) > 2 and p[2] else -1))
            for k in range(1, len(corners) - 1):
                faces.setdefault((obj, mat), []).append((corners[0], corners[k], corners[k + 1]))
    parts = {}
    for key, tris in faces.items():
        remap, pos, nrm, uv, idx = {}, [], [], [], []
        for tri in tris:
            for c in tri:
                if c not in remap:
                    remap[c] = len(pos)
                    pos.append(vs[c[0]])
                    nrm.append(vns[c[2]] if c[2] >= 0 else (0.0, 0.0, 1.0))
                    uv.append(vts[c[1]] if c[1] >= 0 else (0.0, 0.0))
                idx.append(remap[c])
        parts[key] = [(pos, nrm, make_tangents(pos, nrm, uv, idx), uv, idx)]
    return parts


def make_tangents(pos, nrm, uv, idx):
    """Per-vertex tangents from UV gradients, Gram-Schmidt against the normal."""
    t = [[0.0, 0.0, 0.0] for _ in pos]
    b = [[0.0, 0.0, 0.0] for _ in pos]
    for i in range(0, len(idx), 3):
        i0, i1, i2 = idx[i], idx[i + 1], idx[i + 2]
        e1 = [pos[i1][k] - pos[i0][k] for k in range(3)]
        e2 = [pos[i2][k] - pos[i0][k] for k in range(3)]
        du1, dv1 = uv[i1][0] - uv[i0][0], uv[i1][1] - uv[i0][1]
        du2, dv2 = uv[i2][0] - uv[i0][0], uv[i2][1] - uv[i0][1]
        det = du1 * dv2 - du2 * dv1
        if abs(det) < 1e-12:
            continue
        r = 1.0 / det
        sd = [(e1[k] * dv2 - e2[k] * dv1) * r for k in range(3)]
        td = [(e2[k] * du1 - e1[k] * du2) * r for k in range(3)]
        for j in (i0, i1, i2):
            for k in range(3):
                t[j][k] += sd[k]
                b[j][k] += td[k]
    out = []
    for j, n in enumerate(nrm):
        d = sum(t[j][k] * n[k] for k in range(3))
        v = [t[j][k] - n[k] * d for k in range(3)]
        ln = math.sqrt(sum(c * c for c in v))
        if ln < 1e-9:  # no UV gradient: any vector across the normal
            a = (1.0, 0.0, 0.0) if abs(n[0]) < 0.9 else (0.0, 1.0, 0.0)
            v = [a[1] * n[2] - a[2] * n[1], a[2] * n[0] - a[0] * n[2], a[0] * n[1] - a[1] * n[0]]
            ln = math.sqrt(sum(c * c for c in v)) or 1.0
        v = [c / ln for c in v]
        cr = (n[1] * v[2] - n[2] * v[1], n[2] * v[0] - n[0] * v[2], n[0] * v[1] - n[1] * v[0])
        w = -1.0 if sum(cr[k] * b[j][k] for k in range(3)) < 0.0 else 1.0
        out.append((v[0], v[1], v[2], w))
    return out


# ---------------------------------------------------------------- output

def pack_lod(parts, pivots):
    verts, indices, ranges = [], [], []
    for oi, obj in enumerate(OBJECTS):
        px, py, pz = pivots[oi][:3] if oi else (0.0, 0.0, 0.0)
        for mi, mat in enumerate(MATERIALS):
            first = len(indices)
            for pos, nrm, tan, uv, idx in parts.get((obj, mat), []):
                base = len(verts)
                for p, n, t, u in zip(pos, nrm, tan, uv):
                    verts.append((p[0] - px, p[1] - py, p[2] - pz) + tuple(n) + tuple(t) + tuple(u))
                indices.extend(base + i for i in idx)
            if len(indices) > first:
                ranges.append((oi, mi, first, len(indices) - first))
    return verts, indices, ranges


def bounds(lods, pivots):
    lo, hi = [1e9] * 3, [-1e9] * 3
    # wheel vertices are stored around their axles; put them back for the bounds
    verts, indices, ranges = lods[0]
    for oi, _mi, first, count in ranges:
        off = pivots[oi][:3] if oi else (0.0, 0.0, 0.0)
        for i in indices[first:first + count]:
            for k in range(3):
                v = verts[i][k] + off[k]
                lo[k], hi[k] = min(lo[k], v), max(hi[k], v)
    return lo, hi


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("lods", nargs="+", help="lod0 first: .glb (preferred) or .obj files")
    ap.add_argument("-o", "--out", required=True, help="the .rclmesh to write")
    ap.add_argument("--report", help="build_report.json with frame.front/rear_wheel_centre/radius")
    ap.add_argument("--gltf-yup", action="store_true", help="the glTF was exported +y up (Blender's default)")
    args = ap.parse_args()

    pivots = [(0.0, 0.0, 0.0, 0.0), None, None]
    if args.report:
        fr = json.load(open(args.report, encoding="utf-8"))["frame"]
        pivots[1] = tuple(fr["front_wheel_centre"]) + (fr["front_wheel_radius"],)
        pivots[2] = tuple(fr["rear_wheel_centre"]) + (fr["rear_wheel_radius"],)

    lods = []
    for path in args.lods:
        parts = read_glb(path, args.gltf_yup) if path.lower().endswith(".glb") else read_obj(path)
        for oi in (1, 2):
            if pivots[oi] is None:
                # no report: the wheel's axle is the middle of its bounds
                pts = [p for (o, _m), plist in parts.items() if o == OBJECTS[oi] for pp in plist for p in pp[0]]
                lo = [min(p[k] for p in pts) for k in range(3)]
                hi = [max(p[k] for p in pts) for k in range(3)]
                pivots[oi] = tuple((lo[k] + hi[k]) / 2 for k in range(3)) + ((hi[2] - lo[2]) / 2,)
        lods.append(pack_lod(parts, pivots))

    lo, hi = bounds(lods, pivots)
    out = bytearray(MAGIC)
    out += struct.pack("<I", len(lods))
    out += struct.pack("<6f", *(lo + hi))
    for p in pivots:
        out += struct.pack("<4f", *p)
    for verts, indices, ranges in lods:
        if indices and max(indices) >= len(verts):
            raise SystemExit("index past the vertex count")
        out += struct.pack("<III", len(verts), len(indices), len(ranges))
        for oi, mi, first, count in ranges:
            out += struct.pack("<BBHII", oi, mi, 0, first, count)
        for v in verts:
            out += struct.pack("<12f", *v)
        out += struct.pack("<%dI" % len(indices), *indices)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    open(args.out, "wb").write(out)

    print("wrote %s (%d bytes)" % (args.out, len(out)))
    print("bounds min %.3f %.3f %.3f  max %.3f %.3f %.3f" % tuple(lo + hi))
    for name, p in zip(OBJECTS, pivots):
        print("pivot %-11s %.3f %.3f %.3f  r %.3f" % ((name,) + tuple(p)))
    for li, (verts, indices, ranges) in enumerate(lods):
        print("lod%d: %d vertices, %d triangles, %d ranges" % (li, len(verts), len(indices) // 3, len(ranges)))
        for oi, mi, first, count in ranges:
            print("   %-11s %-6s %6d tris" % (OBJECTS[oi], MATERIALS[mi], count // 3))


if __name__ == "__main__":
    sys.exit(main())
