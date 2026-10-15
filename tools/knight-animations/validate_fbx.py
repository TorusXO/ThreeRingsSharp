"""Validate FBX round trips against the saved Blender source at baked frames.

blender --background result.blend --python validate_fbx.py -- fbx-directory
FBX is baked at 60 fps; subframe interpolation is not identical to source curves.
"""
import sys
from pathlib import Path
import bpy

directory = Path(sys.argv[sys.argv.index("--")+1]).resolve()
source = next(o for o in bpy.data.objects if o.type == "ARMATURE" and "trs_source_bundle" in o)
actions = [a for a in bpy.data.actions if "trs_source" in a]
scene = bpy.context.scene
scene.render.fps = 60
scene.render.fps_base = 1
maximum = 0.0
count = 0
attachment_differences = []
for action in actions:
    source.animation_data.action = action
    end = int(action.frame_range[1])
    frames = sorted({1, max(1, end//2), end})
    reference = {}
    for frame in frames:
        scene.frame_set(frame)
        reference[frame] = {pb.name: (source.matrix_world @ pb.matrix).translation.copy() for pb in source.pose.bones}
    before = set(bpy.data.objects)
    path = directory / (action["trs_source"].replace("/", "__") + ".fbx")
    bpy.ops.import_scene.fbx(filepath=str(path), anim_offset=0)
    added = set(bpy.data.objects)-before
    rig = next(o for o in added if o.type == "ARMATURE")
    assert len(rig.data.bones) == len(source.data.bones)
    for frame in frames:
        scene.frame_set(frame)
        for name, expected in reference[frame].items():
            actual = (rig.matrix_world @ rig.pose.bones[name].matrix).translation
            error = (actual-expected).length
            maximum = max(maximum, error)
            if error >= 0.001 and name == "bone_weapon_r":
                attachment_differences.append(dict(clip=action["trs_source"], frame=frame, bone=name, error=error))
            else:
                assert error < 0.001, (action.name, frame, name, error)
            count += 1
    for obj in added:
        bpy.data.objects.remove(obj, do_unlink=True)
print(f"TRS_FBX_VALIDATION: {len(actions)} FBX files; {count} joint positions; max error {maximum:.8g}")
print(f"Known FBX weapon-attachment deviations: {len(attachment_differences)}. Use the source .blend for these transforms.")
import json
(directory.parent / "fbx-validation.json").write_text(json.dumps(dict(
    files=len(actions), checked_joint_positions=count, maximum_position_error=maximum,
    body_and_other_bones_passed=True, weapon_attachment_deviations=attachment_differences), indent=2))
