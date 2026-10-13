"""Validate saved UE retarget Actions against source SK poses and reference rest bones.

blender --background retargeted.blend --python validate_ue_retarget.py -- \
  source-knight-animations.blend original-character.blend report.json
"""

import json
import math
import sys
from pathlib import Path

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
from retarget_ue import target_pose


source_path, reference_path, report_path = (Path(p).resolve() for p in sys.argv[sys.argv.index("--") + 1:])
target = next(o for o in bpy.data.objects if o.type == "ARMATURE" and "pelvis" in o.data.bones)
with bpy.data.libraries.load(str(reference_path), link=False) as (available, loaded):
    loaded.objects = [name for name in available.objects if name == "root"]
reference = loaded.objects[0]
if reference.type != "ARMATURE":
    raise ValueError("Reference root is not an armature")
original = {bone.name: bone for bone in reference.data.bones}
if set(original) != set(target.data.bones.keys()):
    raise AssertionError("Target bone names changed")
maximum_rest_error = 0.0
for bone in target.data.bones:
    other = original[bone.name]
    if (bone.parent.name if bone.parent else None) != (other.parent.name if other.parent else None):
        raise AssertionError(f"Target bone parent changed: {bone.name}")
    maximum_rest_error = max(maximum_rest_error, max(abs(bone.matrix_local[r][c] - other.matrix_local[r][c])
                                                  for r in range(4) for c in range(4)))

with bpy.data.libraries.load(str(source_path), link=False) as (available, loaded):
    loaded.objects = [name for name in available.objects if name == "Spiral Knight"]
    loaded.actions = list(available.actions)
source = loaded.objects[0]
bpy.context.collection.objects.link(source)
source.animation_data_create()
source_actions = {a.get("trs_source"): a for a in loaded.actions if "trs_source" in a}
target_actions = {a.get("trs_source"): a for a in bpy.data.actions
                  if a.name.startswith("UE_") and "trs_retarget_method" in a}
if not target_actions or not set(target_actions).issubset(source_actions):
    raise AssertionError("Retarget Actions are missing or lack source clips")

rest = {bone.name: bone.matrix_local.copy() for bone in target.data.bones}
maximum_position_error = 0.0
maximum_angular_error = 0.0
pose_checks = 0
for name, action in target_actions.items():
    source_action = source_actions[name]
    frames = [float(key.co.x) for key in source_action.fcurves[0].keyframe_points]
    source.animation_data.action = source_action
    target.animation_data.action = action
    for frame in (frames[0], frames[len(frames) // 2], frames[-1]):
        bpy.context.scene.frame_set(math.floor(frame), subframe=frame - math.floor(frame))
        expected = target_pose(source, target, rest, action["trs_root_motion_scale"])
        for bone in target.pose.bones:
            actual, wanted = bone.matrix, expected[bone.name]
            maximum_position_error = max(maximum_position_error,
                                         (actual.translation - wanted.translation).length)
            maximum_angular_error = max(maximum_angular_error,
                                        actual.to_quaternion().rotation_difference(wanted.to_quaternion()).angle)
            pose_checks += 1
if maximum_rest_error > 1e-6 or maximum_position_error > 0.001 or maximum_angular_error > 0.002:
    raise AssertionError(f"Retarget mismatch: rest {maximum_rest_error}, position {maximum_position_error}, angle {maximum_angular_error}")

unmapped_weights = []
for mesh in bpy.data.objects:
    if mesh.type != "MESH" or not any(m.type == "ARMATURE" and m.object == target for m in mesh.modifiers):
        continue
    missing = 0.0
    for vertex in mesh.data.vertices:
        for weight in vertex.groups:
            if mesh.vertex_groups[weight.group].name not in target.data.bones:
                missing += weight.weight
    unmapped_weights.append({"mesh": mesh.name, "vertices": len(mesh.data.vertices),
                             "unmapped_weight_sum": missing})
if any(item["unmapped_weight_sum"] > 0.0001 for item in unmapped_weights):
    raise AssertionError("Skinned mesh still has nonzero weights for missing target bones")

report = {"format": "trs-ue-retarget-validation-1", "source": str(source_path),
          "reference": str(reference_path), "target_bones": len(rest),
          "retargeted_actions": len(target_actions), "pose_checks": pose_checks,
          "maximum_rest_matrix_error": maximum_rest_error,
          "maximum_pose_position_error": maximum_position_error,
          "maximum_pose_angular_error_radians": maximum_angular_error,
          "meshes": unmapped_weights}
report_path.parent.mkdir(parents=True, exist_ok=True)
report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
print(f"UE_RETARGET_VALIDATION: {len(target_actions)} Actions; {pose_checks} poses; "
      f"position {maximum_position_error:.8g}; angle {maximum_angular_error:.8g}; weights mapped", flush=True)
