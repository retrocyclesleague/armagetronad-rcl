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
how). Files of an older pack that this one lacks are removed, so the folder holds one pack.
"""
import glob
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
MAPS = ["normal", "basecolor", "ao", "roughness", "mask"]


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
    for lod in range(len(lods)):
        for m in MAPS:
            found = glob.glob(os.path.join(pack, "*_%s.png" % m)) if lod == 0 else \
                glob.glob(os.path.join(pack, "*_lod%d_%s.png" % (lod, m)))
            found = [f for f in found if lod > 0 or "_lod" not in os.path.basename(f)]
            if found:
                put(found[0], "lightcycle_lod%d_%s.png" % (lod, m))

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
