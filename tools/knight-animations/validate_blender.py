"""Validate evaluated Blender poses against the exported source matrices.

blender --background result.blend --python validate_blender.py -- bundle.json
"""
import json
import sys
from pathlib import Path
import bpy
from mathutils import Matrix

data = json.loads(Path(sys.argv[sys.argv.index("--")+1]).read_text(encoding="utf-8"))
rig = next(o for o in bpy.data.objects if o.type == "ARMATURE" and "trs_source_bundle" in o)
bones = data["bones"]
indices = {b["name"]: i for i, b in enumerate(bones)}
actions = {a.get("trs_source"): a for a in bpy.data.actions if "trs_source" in a}
assert len(actions) == len(data["clips"])
maximum = 0.0
checks = 0
for clip in data["clips"]:
    rig.animation_data.action = actions[clip["name"]]
    for sample_index in sorted({0, len(clip["frames"])//2, len(clip["frames"])-1}):
        frame = 1 + sample_index * 60 / clip["rate"]
        bpy.context.scene.frame_set(int(frame), subframe=frame-int(frame))
        local = [Matrix(b["matrix"]) for b in bones]
        for name, value in zip(clip["targets"], clip["frames"][sample_index]):
            if name in indices:
                local[indices[name]] = Matrix(value)
        world = []
        for i, bone in enumerate(bones):
            p = bone["parent"]
            expected = local[i] if p < 0 else world[p] @ local[i]
            world.append(expected)
            actual = rig.pose.bones[bone["name"]].matrix
            error = max(abs(actual[r][c]-expected[r][c]) for r in range(4) for c in range(4))
            maximum = max(maximum, error)
            assert error < 0.001, (clip["name"], sample_index, bone["name"], error)
            checks += 1
print(f"TRS_VALIDATION: {len(actions)} Actions; {checks} evaluated bone poses; max error {maximum:.8g}")
