"""Blender 4.2 LTS: import a TRS knight animation bundle as an armature and Actions.

Run: blender --background --python import_blender.py -- bundle.json result.blend
Optional third argument: an existing output directory for per-action FBX files.
The current scene is preserved. No mesh or game assets are downloaded.
"""
import json
import math
import sys
from pathlib import Path

import bpy
from mathutils import Matrix


def import_bundle(bundle_path):
    if bpy.app.version >= (4, 4, 0):
        raise RuntimeError("Run this importer in Blender 4.2 LTS; the resulting .blend can be opened in newer Blender versions. Layered Action authoring is not implemented here.")
    data = json.loads(Path(bundle_path).read_text(encoding="utf-8"))
    if data.get("format") != "trs-knight-animations-1":
        raise ValueError("Unsupported knight animation bundle")
    bones = data["bones"]
    names = [b["name"] for b in bones]
    if len(set(names)) != len(names):
        raise ValueError("Duplicate bone names")
    rest_local = [Matrix(b["matrix"]) for b in bones]
    rest_world = []
    for i, bone in enumerate(bones):
        parent = bone["parent"]
        if not -1 <= parent < i:
            raise ValueError("Skeleton must be in parent-before-child order")
        rest_world.append(rest_local[i] if parent < 0 else rest_world[parent] @ rest_local[i])

    armature = bpy.data.armatures.new("Spiral Knight")
    rig = bpy.data.objects.new("Spiral Knight", armature)
    bpy.context.collection.objects.link(rig)
    bpy.ops.object.select_all(action="DESELECT")
    rig.select_set(True)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.mode_set(mode="EDIT")
    for i, bone in enumerate(bones):
        edit = armature.edit_bones.new(bone["name"])
        edit.length = 0.06
        # Edit bones cannot contain scale/reflections. Pose bases below compensate.
        location, rotation, _ = rest_world[i].decompose()
        edit.matrix = Matrix.LocRotScale(location, rotation, None)
        if bone["parent"] >= 0:
            edit.parent = armature.edit_bones[names[bone["parent"]]]
        edit.use_connect = False
    bpy.ops.object.mode_set(mode="OBJECT")
    rig.show_in_front = True
    rig.animation_data_create()
    scene = bpy.context.scene
    scene.render.fps = 60
    scene.render.fps_base = 1.0
    pose = [rig.pose.bones[n] for n in names]
    for pb in pose:
        pb.rotation_mode = "QUATERNION"
    index = {name: i for i, name in enumerate(names)}
    actions = []
    max_error = 0.0
    for clip in data["clips"]:
        rate = clip["rate"]
        if not math.isfinite(rate) or rate <= 0:
            raise ValueError(f"Invalid frame rate: {clip['name']}")
        samples = list(clip["frames"])
        # skipLastFrame is an import-time option already applied to stored transforms.
        # A looping clip needs an explicit wrap key to preserve its complete period.
        if clip["loop"] and len(samples) > 1:
            samples.append(samples[0])
        action = bpy.data.actions.new(clip["name"])
        action.use_fake_user = True
        action["trs_source"] = clip["name"]
        action["trs_loop"] = clip["loop"]
        action["trs_rate"] = rate
        rig.animation_data.action = action
        curves = {}
        for pb in pose:
            for prop, count in (("location", 3), ("rotation_quaternion", 4), ("scale", 3)):
                path = pb.path_from_id(prop)
                for channel in range(count):
                    fc = action.fcurves.new(path, index=channel, action_group=pb.name)
                    curves[pb.name, prop, channel] = (fc, [])
        previous = {}
        for frame_index, sample in enumerate(samples):
            local = list(rest_local)
            for target, matrix in zip(clip["targets"], sample):
                if target in index:
                    if matrix is None:
                        raise ValueError(f"Missing transform: {clip['name']} frame {frame_index} {target}")
                    local[index[target]] = Matrix(matrix)
            world = []
            for i, pb in enumerate(pose):
                parent = bones[i]["parent"]
                world.append(local[i] if parent < 0 else world[parent] @ local[i])
                kwargs = {} if parent < 0 else dict(parent_matrix=world[parent], parent_matrix_local=pose[parent].bone.matrix_local)
                basis = pb.bone.convert_local_to_pose(world[i], pb.bone.matrix_local, invert=True, **kwargs)
                loc, rot, scale = basis.decompose()
                if pb.name in previous and rot.dot(previous[pb.name]) < 0:
                    rot.negate()
                previous[pb.name] = rot.copy()
                reconstructed = pb.bone.convert_local_to_pose(Matrix.LocRotScale(loc, rot, scale), pb.bone.matrix_local, **kwargs)
                max_error = max(max_error, max(abs(reconstructed[r][c]-world[i][r][c]) for r in range(4) for c in range(4)))
                frame = 1 + frame_index * 60 / rate
                for prop, value in (("location", loc), ("rotation_quaternion", rot), ("scale", scale)):
                    for channel, component in enumerate(value):
                        curves[pb.name, prop, channel][1].extend((frame, component))
        for fc, values in curves.values():
            fc.keyframe_points.add(len(values)//2)
            fc.keyframe_points.foreach_set("co", values)
            for key in fc.keyframe_points:
                key.interpolation = "LINEAR"
            fc.update()
        actions.append(action)
    rig.animation_data.action = next((a for a in actions if a.name == "run_forward"), actions[0])
    scene.frame_start = 1
    scene.frame_end = math.ceil(rig.animation_data.action.frame_range[1])
    scene.frame_set(1)
    rig["trs_max_matrix_error"] = max_error
    rig["trs_source_bundle"] = str(Path(bundle_path).resolve())
    print(f"TRS_IMPORT: {len(bones)} bones, {len(actions)} Actions; max matrix reconstruction error {max_error:.8g}")
    if max_error > 0.001:
        raise ValueError("Transform reconstruction error exceeds tolerance; inspect skeleton scale/shear")
    return rig, actions


def export_fbx(rig, actions, directory):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    scene = bpy.context.scene
    for action in actions:
        rig.animation_data.action = action
        scene.frame_start = 1
        scene.frame_end = math.ceil(action.frame_range[1])
        path = directory / (action["trs_source"].replace("/", "__") + ".fbx")
        if path.exists():
            raise FileExistsError(path)
        bpy.ops.export_scene.fbx(filepath=str(path), use_selection=True,
            object_types={"ARMATURE"}, add_leaf_bones=False, bake_anim=True,
            bake_anim_use_all_actions=False, bake_anim_use_nla_strips=False,
            bake_anim_simplify_factor=0.0, axis_forward="-Z", axis_up="Y")


if __name__ == "__main__":
    args = sys.argv[sys.argv.index("--")+1:] if "--" in sys.argv else []
    if len(args) not in (2, 3):
        raise SystemExit("Expected: -- bundle.json result.blend [fbx-directory]")
    destination = Path(args[1]).resolve()
    if destination.exists():
        raise FileExistsError(destination)
    rig, actions = import_bundle(args[0])
    destination.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(destination))
    if len(args) == 3:
        export_fbx(rig, actions, args[2])
