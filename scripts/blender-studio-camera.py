"""Set a Blender camera to the colour studio's preview camera.

The game writes the camera with RCL_STUDIO_CAMERA (or the C key in the colour
studio) to var/studio-camera.json in the player's profile. This script puts a
camera named "RCL_studio" where it was, in the frame the light cycle model is
built in (+x ahead, +y left, +z up, the cycle at the origin unrotated), with
the same vertical field of view and picture size, so a render can be set
beside an in-game frame of the same view.

  blender model.blend --python scripts/blender-studio-camera.py -- studio-camera.json
  blender -b model.blend --python scripts/blender-studio-camera.py -- studio-camera.json --render out.png

The model must stand at the origin, unrotated, in game units (the export in
rcl-cfg/art/lightcycle does). The game's preview holds still only with
RCL_STUDIO_HOLD 1 (at RCL_STUDIO_SWING degrees); otherwise the file has the
angle the cycle had when it was written.
"""

import json
import math
import sys

import bpy
from mathutils import Vector


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if not argv:
        raise SystemExit("usage: ... --python blender-studio-camera.py -- studio-camera.json [--render out.png]")
    with open(argv[0], encoding="utf-8") as f:
        pose = json.load(f)
    out = argv[argv.index("--render") + 1] if "--render" in argv else None

    scene = bpy.context.scene
    data = bpy.data.cameras.get("RCL_studio") or bpy.data.cameras.new("RCL_studio")
    camera = bpy.data.objects.get("RCL_studio")
    if camera is None:
        camera = bpy.data.objects.new("RCL_studio", data)
        scene.collection.objects.link(camera)

    eye = Vector(pose["camera"]["eye"])
    target = Vector(pose["camera"]["target"])
    camera.location = eye
    # looking down -Z with +Y up, the up being the sky, as the game's look-at has it
    camera.rotation_mode = "QUATERNION"
    camera.rotation_quaternion = (target - eye).to_track_quat("-Z", "Y")

    data.type = "PERSP"
    data.sensor_fit = "VERTICAL"
    data.angle_y = math.radians(pose["fov_y_degrees"])
    data.clip_start = pose["clip"]["near"]
    data.clip_end = min(pose["clip"]["far"], 10000.0)
    data.shift_x = data.shift_y = 0.0

    scene.camera = camera
    scene.render.resolution_x = int(pose["image"]["width"])
    scene.render.resolution_y = int(pose["image"]["height"])
    scene.render.resolution_percentage = 100
    scene.render.pixel_aspect_x = scene.render.pixel_aspect_y = 1.0

    print("RCL_studio camera: eye", tuple(round(v, 4) for v in eye), "target", tuple(round(v, 4) for v in target),
          "fov_y", pose["fov_y_degrees"], "image", pose["image"]["width"], "x", pose["image"]["height"],
          "swing", pose.get("swing_degrees"), "look", pose.get("look"))

    if out:
        scene.render.filepath = out
        bpy.ops.render.render(write_still=True)


main()
