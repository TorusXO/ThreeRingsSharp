"""Rename source bones and all TRS Action paths, preserving hierarchy and motion.

blender --background source.blend --python rename_ue_bones.py -- renamed.blend
Names were checked against the user's 88-bone Cascadeur reference FBX.
This is name mapping, not retargeting to that different rest pose/hierarchy.
"""
import json
import sys
from pathlib import Path
import bpy

MAPPING = {
    "Bip01 Pelvis": "pelvis",
    "Bip01 Spine": "spine_01",
    "Bip01 Spine1": "spine_05",  # source upper torso -> reference upper torso
    "Bip01 Neck": "neck_01",
    "Bip01 Head": "head",
}
for source, target in (("Clavicle", "clavicle"), ("UpperArm", "upperarm"),
                       ("Forearm", "lowerarm"), ("Hand", "hand"),
                       ("Thigh", "thigh"), ("Calf", "calf"), ("Foot", "foot"),
                       ("Finger0", "thumb_01"), ("Finger01", "thumb_02"),
                       ("Finger1", "index_01"), ("Finger11", "index_02")):
    for side in ("L", "R"):
        MAPPING[f"Bip01 {side} {source}"] = f"{target}_{side.lower()}"


def rename(rig):
    mapping = {old: new for old, new in MAPPING.items() if old in rig.data.bones}
    for old, new in mapping.items():
        if new in rig.data.bones and new not in mapping:
            raise ValueError(f"Bone-name collision: {new}")
    # Snapshot paths because Blender may update the currently assigned Action itself.
    paths = [(fc, fc.data_path) for a in bpy.data.actions if "trs_source" in a for fc in a.fcurves]
    for old, new in mapping.items():
        rig.data.bones[old].name = new
    for fc, path in paths:
        for old, new in mapping.items():
            prefix = 'pose.bones[' + json.dumps(old) + ']'
            if path.startswith(prefix):
                fc.data_path = 'pose.bones[' + json.dumps(new) + ']' + path[len(prefix):]
                break
    for action in bpy.data.actions:
        if "trs_source" in action:
            for group in action.groups:
                if group.name in mapping:
                    group.name = mapping[group.name]
    for obj in bpy.data.objects:
        if obj.type == "MESH" and any(m.type == "ARMATURE" and m.object == rig for m in obj.modifiers):
            for old, new in mapping.items():
                if old in obj.vertex_groups:
                    if new in obj.vertex_groups:
                        raise ValueError(f"Vertex-group collision on {obj.name}: {new}")
                    obj.vertex_groups[old].name = new
    rig["trs_bone_name_map"] = json.dumps(mapping)
    return mapping


if __name__ == "__main__":
    destination = Path(sys.argv[sys.argv.index("--")+1]).resolve()
    if destination.exists():
        raise FileExistsError(destination)
    rig = next(o for o in bpy.data.objects if o.type == "ARMATURE" and "trs_source_bundle" in o)
    mapping = rename(rig)
    bpy.context.scene.frame_set(bpy.context.scene.frame_current)
    bpy.ops.wm.save_as_mainfile(filepath=str(destination))
    print(f"Renamed {len(mapping)} bones and associated TRS Action paths; source hierarchy retained.")
