"""Bake TRS Actions onto the armature and weights of an open Blender character.

Run with Blender 4.2 LTS and the destination character file already open:
  blender --background character.blend --python retarget_ue.py -- \
    source-knight-animations.blend output.blend report.json

The source and destination files are read only. The output keeps the destination
armature's bone names, rest pose, hierarchy, mesh data, and vertex weights.
"""

import argparse
import hashlib
import json
import math
import re
import sys
from pathlib import Path

import bpy
from mathutils import Matrix, Quaternion


def args_from_blender():
    parser = argparse.ArgumentParser()
    parser.add_argument("source")
    parser.add_argument("destination")
    parser.add_argument("report")
    parser.add_argument("--clips", help="Comma-separated source clip names; default is all")
    parser.add_argument("--root-motion-scale", type=float, default=1.0)
    parser.add_argument("--fbx-directory", help="Optional UE skeletal mesh and per-clip animation FBX output")
    parser.add_argument("--fbx-clips", help="Comma-separated names to export as FBX; default is every retargeted clip")
    return parser.parse_args(sys.argv[sys.argv.index("--") + 1:])


def lerp_delta(deltas, first, last, amount):
    return deltas[first].slerp(deltas[last], min(1.0, max(0.0, amount)))


def ratio_between(rest, current, first, last):
    start, end = rest[first].translation, rest[last].translation
    line = end - start
    return (rest[current].translation - start).dot(line) / max(line.length_squared, 1e-10)


def rotation_deltas(source, target_rest):
    rest = {bone.name: bone.matrix_local for bone in source.data.bones}
    deltas = {
        name: source.pose.bones[name].matrix.to_quaternion() @ matrix.to_quaternion().inverted()
        for name, matrix in rest.items()
    }
    result = {}
    for name in target_rest:
        if name in deltas:
            result[name] = deltas[name]
        elif name in {"spine_02", "spine_03", "spine_04"}:
            result[name] = lerp_delta(deltas, "spine_01", "spine_05",
                                           ratio_between(target_rest, name, "spine_01", "spine_05"))
        elif name == "neck_02":
            result[name] = lerp_delta(deltas, "neck_01", "head",
                                           ratio_between(target_rest, name, "neck_01", "head"))
        elif "_twist_" in name:
            base, side = name.split("_twist_")[0], name[-1]
            successor = {"upperarm": "lowerarm", "lowerarm": "hand",
                         "thigh": "calf", "calf": "foot"}[base]
            first, last = f"{base}_{side}", f"{successor}_{side}"
            result[name] = lerp_delta(deltas, first, last,
                                           ratio_between(target_rest, name, first, last))
        elif name.startswith(("index_", "middle_", "ring_", "pinky_")):
            side = name[-1]
            if "metacarpal" in name:
                result[name] = lerp_delta(deltas, f"hand_{side}", f"index_01_{side}", 0.2)
            else:
                joint = name.split("_")[1]
                result[name] = deltas[f"index_{'01' if joint == '01' else '02'}_{side}"]
        elif name.startswith("thumb_03_"):
            result[name] = deltas[f"thumb_02_{name[-1]}"]
        elif name.startswith("ball_"):
            result[name] = deltas[f"foot_{name[-1]}"]
        elif name.startswith("ik_foot_") and name[-1] in "lr":
            result[name] = deltas[f"foot_{name[-1]}"]
        elif name.startswith("ik_hand_") and name[-1] in "lr":
            result[name] = deltas[f"hand_{name[-1]}"]
        elif name == "ik_hand_gun":
            result[name] = deltas["hand_r"]
        else:
            result[name] = None  # Keep the rest orientation relative to its parent.
    return rest, result


def ordered_bones(armature):
    ordered = []
    def add(bone):
        ordered.append(bone)
        for child in bone.children:
            add(child)
    for bone in armature.data.bones:
        if bone.parent is None:
            add(bone)
    return ordered


def target_pose(source, target, target_rest, root_motion_scale):
    source_rest, deltas = rotation_deltas(source, target_rest)
    positions, rotations, desired = {}, {}, {}
    rest_quats = {name: matrix.to_quaternion() for name, matrix in target_rest.items()}
    hand_positions = {}
    foot_positions = {}
    for bone in ordered_bones(target):
        name = bone.name
        parent = bone.parent.name if bone.parent else None
        delta = deltas[name] or (rotations[parent] @ rest_quats[parent].inverted() if parent else Quaternion())
        delta.normalize()
        rotations[name] = delta @ rest_quats[name]
        if name == "pelvis":
            movement = source.pose.bones["pelvis"].matrix.translation - source_rest["pelvis"].translation
            positions[name] = target_rest[name].translation + movement * root_motion_scale
        elif parent:
            parent_delta = rotations[parent] @ rest_quats[parent].inverted()
            offset = target_rest[name].translation - target_rest[parent].translation
            positions[name] = positions[parent] + parent_delta @ offset
        else:
            positions[name] = target_rest[name].translation.copy()
        if name.startswith("hand_"):
            hand_positions[name[-1]] = positions[name].copy()
        elif name.startswith("foot_"):
            foot_positions[name[-1]] = positions[name].copy()
        elif name == "ik_hand_gun":
            positions[name] = hand_positions["r"].copy()
        elif name.startswith("ik_hand_") and name[-1] in "lr":
            positions[name] = hand_positions[name[-1]].copy()
        elif name.startswith("ik_foot_") and name[-1] in "lr":
            positions[name] = foot_positions[name[-1]].copy()
        desired[name] = Matrix.LocRotScale(positions[name], rotations[name], None)
    return desired


def retarget_action(source, target, source_action, root_motion_scale):
    target_rest = {bone.name: bone.matrix_local.copy() for bone in target.data.bones}
    frames = sorted({float(key.co.x) for key in source_action.fcurves[0].keyframe_points})
    if not frames:
        raise ValueError(f"No keyframes in {source_action.name}")
    action = bpy.data.actions.new("UE_" + source_action.name)
    action.use_fake_user = True
    for key in ("trs_source", "trs_loop", "trs_rate"):
        if key in source_action:
            action[key] = source_action[key]
    action["trs_retarget_method"] = "rest-space rotation with preserved target bone lengths"
    action["trs_root_motion_scale"] = root_motion_scale
    target.animation_data.action = action
    source.animation_data.action = source_action
    curves = {}
    previous = {}
    maximum_error = 0.0
    for frame in frames:
        bpy.context.scene.frame_set(math.floor(frame), subframe=frame - math.floor(frame))
        desired = target_pose(source, target, target_rest, root_motion_scale)
        for bone in ordered_bones(target):
            name, parent = bone.name, bone.parent
            pose = target.pose.bones[name]
            kwargs = {} if parent is None else {
                "parent_matrix": desired[parent.name],
                "parent_matrix_local": target_rest[parent.name],
            }
            basis = pose.bone.convert_local_to_pose(
                desired[name], target_rest[name], invert=True, **kwargs)
            location, rotation, scale = basis.decompose()
            if name in previous and rotation.dot(previous[name]) < 0:
                rotation.negate()
            previous[name] = rotation.copy()
            recovered = pose.bone.convert_local_to_pose(
                Matrix.LocRotScale(location, rotation, scale), target_rest[name], **kwargs)
            maximum_error = max(maximum_error, max(
                abs(recovered[row][column] - desired[name][row][column])
                for row in range(4) for column in range(4)))
            for prop, values in (
                ("location", location),
                ("rotation_quaternion", rotation),
                ("scale", scale),
            ):
                for channel, value in enumerate(values):
                    key = (name, prop, channel)
                    entry = curves.setdefault(key, {"points": []})
                    entry["points"].extend((frame, float(value)))
    for (name, prop, channel), entry in curves.items():
        fc = action.fcurves.new(target.pose.bones[name].path_from_id(prop), index=channel, action_group=name)
        fc.keyframe_points.add(len(frames))
        fc.keyframe_points.foreach_set("co", entry["points"])
        for key in fc.keyframe_points:
            key.interpolation = "LINEAR"
        fc.update()
    if maximum_error > 0.001:
        raise ValueError(f"Retarget reconstruction error in {source_action.name}: {maximum_error}")
    return action, len(frames), maximum_error


def remap_missing_belt_weights(target):
    changes = []
    for mesh in bpy.data.objects:
        if mesh.type != "MESH" or not any(m.type == "ARMATURE" and m.object == target for m in mesh.modifiers):
            continue
        for source_name in ("Bone belt_front", "Bone belt_rear", "Bone belt_l", "Bone belt_r"):
            group = mesh.vertex_groups.get(source_name)
            if group is None:
                continue
            weights = [(vertex.index, next((item.weight for item in vertex.groups if item.group == group.index), 0.0))
                       for vertex in mesh.data.vertices]
            weights = [(index, value) for index, value in weights if value > 0]
            if not weights:
                continue
            pelvis = mesh.vertex_groups.get("pelvis")
            if pelvis is None:
                group.name = "pelvis"
            else:
                for index, value in weights:
                    pelvis.add([index], value, "ADD")
                mesh.vertex_groups.remove(group)
            changes.append({"mesh": mesh.name, "from": source_name, "to": "pelvis",
                            "vertices": len(weights), "weight_sum": sum(value for _, value in weights)})
    return changes


def weight_coverage(target):
    result = []
    names = set(target.data.bones.keys())
    for mesh in bpy.data.objects:
        if mesh.type != "MESH" or not any(m.type == "ARMATURE" and m.object == target for m in mesh.modifiers):
            continue
        missing = {}
        for vertex in mesh.data.vertices:
            for item in vertex.groups:
                name = mesh.vertex_groups[item.group].name
                if name not in names:
                    missing[name] = missing.get(name, 0.0) + item.weight
        missing = {name: weight for name, weight in missing.items() if weight > 1e-4}
        result.append({"mesh": mesh.name, "vertices": len(mesh.data.vertices),
                       "unmapped_weight_groups": missing})
    return result


def export_ue_fbx(target, actions, directory, chosen=None):
    directory = Path(directory).resolve()
    directory.mkdir(parents=True, exist_ok=True)
    scene = bpy.context.scene
    meshes = [obj for obj in bpy.data.objects if obj.type == "MESH" and
              any(mod.type == "ARMATURE" and mod.object == target for mod in obj.modifiers)]
    if not meshes:
        raise ValueError("UE FBX requires at least one mesh skinned to the destination armature")
    hidden_meshes = {mesh.name: mesh.hide_get() for mesh in meshes}
    for mesh in meshes:
        mesh.hide_set(False)
    bpy.ops.object.select_all(action="DESELECT")
    target.select_set(True)
    for mesh in meshes:
        mesh.select_set(True)
    bpy.context.view_layer.objects.active = target
    target.animation_data.action = None
    for bone in target.pose.bones:
        bone.location = (0, 0, 0)
        bone.rotation_quaternion = (1, 0, 0, 0)
        bone.scale = (1, 1, 1)
    scene.frame_set(1)
    base = directory / "UE_SkeletalMesh.fbx"
    bpy.ops.export_scene.fbx(filepath=str(base), use_selection=True,
                             object_types={"ARMATURE", "MESH"}, add_leaf_bones=False,
                             bake_anim=False, axis_forward="-Z", axis_up="Y")
    for mesh in meshes:
        mesh.select_set(False)
        mesh.hide_set(hidden_meshes[mesh.name])
    result = [base.name]
    names = set(actions if chosen is None else chosen)
    if not names.issubset(actions):
        raise ValueError("One or more selected UE FBX clips are unavailable")
    for index, name in enumerate(sorted(names)):
        action = actions[name]
        target.animation_data.action = action
        scene.frame_start = 1
        scene.frame_end = math.ceil(action.frame_range[1])
        scene.frame_set(1)
        slug = re.sub(r"[^A-Za-z0-9_.-]+", "_", name).strip("._")[:90]
        digest = hashlib.sha256(name.encode("utf-8")).hexdigest()[:8]
        path = directory / f"UE_{slug}_{digest}.fbx"
        bpy.ops.export_scene.fbx(filepath=str(path), use_selection=True,
                                 object_types={"ARMATURE"}, add_leaf_bones=False,
                                 bake_anim=True, bake_anim_use_all_actions=False,
                                 bake_anim_use_nla_strips=False, bake_anim_simplify_factor=0.0,
                                 axis_forward="-Z", axis_up="Y")
        result.append(path.name)
        if (index + 1) % 25 == 0:
            print(f"UE_FBX_PROGRESS: {index + 1}/{len(names)}", flush=True)
    return result


def main():
    args = args_from_blender()
    if bpy.app.version[:2] != (4, 2):
        raise RuntimeError("Retargeting uses Blender 4.2 LTS Action F-curves")
    if not math.isfinite(args.root_motion_scale) or args.root_motion_scale <= 0:
        raise ValueError("Root motion scale must be positive and finite")
    source_path, destination, report_path = map(lambda p: Path(p).resolve(),
                                                (args.source, args.destination, args.report))
    if destination.exists():
        raise FileExistsError(destination)
    target = next((obj for obj in bpy.data.objects if obj.type == "ARMATURE" and
                   "pelvis" in obj.data.bones and "spine_05" in obj.data.bones), None)
    if target is None:
        raise ValueError("Open a character .blend with the UE pelvis/spine_05 armature")
    if not any(obj.type == "MESH" and any(mod.type == "ARMATURE" and mod.object == target for mod in obj.modifiers)
               for obj in bpy.data.objects):
        raise ValueError("The selected UE character has no mesh skinned to its armature")
    original_bones = [(bone.name, bone.parent.name if bone.parent else None,
                       tuple(tuple(row) for row in bone.matrix_local)) for bone in target.data.bones]
    with bpy.data.libraries.load(str(source_path), link=False) as (available, loaded):
        loaded.objects = [name for name in available.objects if name == "Spiral Knight"]
        loaded.actions = list(available.actions)
    if len(loaded.objects) != 1:
        raise ValueError("Source .blend does not contain the Spiral Knight armature")
    source = loaded.objects[0]
    bpy.context.collection.objects.link(source)
    source.animation_data_create()
    target.animation_data_create()
    for pose in target.pose.bones:
        pose.rotation_mode = "QUATERNION"
    source_actions = sorted((action for action in loaded.actions if "trs_source" in action),
                            key=lambda action: action.name)
    if args.clips:
        selected = set(args.clips.split(","))
        source_actions = [action for action in source_actions if action.get("trs_source") in selected]
        if len(source_actions) != len(selected):
            raise ValueError("One or more requested clip names are missing")
    if not source_actions:
        raise ValueError("No source TRS Actions found")
    if not all(name in source.data.bones for name in ("pelvis", "spine_01", "spine_05", "neck_01", "head")):
        raise ValueError("Source rig is missing the knight retarget anchors")
    rows = []
    retargeted = {}
    for index, source_action in enumerate(source_actions):
        action, frame_count, error = retarget_action(source, target, source_action, args.root_motion_scale)
        retargeted[source_action.get("trs_source")] = action
        rows.append({"clip": source_action.get("trs_source"), "frames": frame_count,
                     "fcurves": len(action.fcurves), "reconstruction_error": error})
        if (index + 1) % 25 == 0:
            print(f"RETARGET_PROGRESS: {index + 1}/{len(source_actions)}", flush=True)
    target.animation_data.action = retargeted.get("run_forward", next(iter(retargeted.values())))
    scene = bpy.context.scene
    scene.render.fps = 60
    scene.render.fps_base = 1.0
    scene.frame_start = 1
    scene.frame_end = math.ceil(target.animation_data.action.frame_range[1])
    scene.frame_set(1)
    belt_weights = remap_missing_belt_weights(target)
    coverage = weight_coverage(target)
    if any(item["unmapped_weight_groups"] for item in coverage):
        raise ValueError(f"Target meshes have weights for missing UE bones: {coverage}")
    source_bone_names = set(source.data.bones.keys())
    source.animation_data.action = None
    bpy.data.objects.remove(source, do_unlink=True)
    for action in loaded.actions:
        bpy.data.actions.remove(action, do_unlink=True)
    result_bones = [(bone.name, bone.parent.name if bone.parent else None,
                     tuple(tuple(row) for row in bone.matrix_local)) for bone in target.data.bones]
    if result_bones != original_bones:
        raise AssertionError("The UE armature rest pose or hierarchy changed")
    report = {
        "format": "trs-ue-retarget-1", "source": str(source_path),
        "destination": str(destination), "target_armature": target.name,
        "source_bones": len(source_bone_names), "target_bones": len(target.data.bones), "clips": len(rows),
        "direct_bone_names": sorted(source_bone_names & set(target.data.bones.keys())),
        "derived_or_rest_bone_names": sorted(set(target.data.bones.keys()) - source_bone_names),
        "source_only_bone_names": sorted(source_bone_names - set(target.data.bones.keys())),
        "root_motion_scale": args.root_motion_scale,
        "maximum_reconstruction_error": max(row["reconstruction_error"] for row in rows),
        "belt_weights_to_pelvis": belt_weights, "mesh_weight_coverage": coverage,
        "actions": rows,
    }
    if args.fbx_directory:
        chosen = set(args.fbx_clips.split(",")) if args.fbx_clips else None
        report["fbx_files"] = export_ue_fbx(target, retargeted, args.fbx_directory, chosen)
        target.animation_data.action = retargeted.get("run_forward", next(iter(retargeted.values())))
        scene.frame_start = 1
        scene.frame_end = math.ceil(target.animation_data.action.frame_range[1])
        scene.frame_set(1)
    destination.parent.mkdir(parents=True, exist_ok=True)
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    bpy.ops.wm.save_as_mainfile(filepath=str(destination))
    print(f"RETARGET_RESULT: {len(rows)} Actions on {target.name}; {len(target.data.bones)} bones; {destination}", flush=True)


if __name__ == "__main__":
    main()
