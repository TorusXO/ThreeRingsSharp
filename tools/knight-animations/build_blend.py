"""Qt pipeline entrypoint. Creates a fresh scene; never edits an existing blend."""
import argparse
import json
import sys
from pathlib import Path

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
from import_blender import import_bundle, export_fbx
from import_scene_blender import import_scene
from rename_ue_bones import rename

parser = argparse.ArgumentParser()
parser.add_argument("mode", choices=["animation", "scene", "skybox"])
parser.add_argument("bundle")
parser.add_argument("destination")
parser.add_argument("--ue-names", action="store_true")
parser.add_argument("--fbx", action="store_true")
parser.add_argument("--color-node", help="Optional .blend library containing ColorReplacementNode")
args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:])
destination = Path(args.destination).resolve()
if destination.exists():
    raise FileExistsError(destination)
if bpy.app.version[:2] != (4, 2):
    raise RuntimeError("This exporter is validated with Blender 4.2 LTS; select a 4.2 executable.")
bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
if args.mode == "animation":
    rig, actions = import_bundle(args.bundle)
    if args.ue_names:
        rename(rig)
    if args.fbx:
        export_fbx(rig, actions, destination.parent / "fbx")
else:
    import_scene(args.bundle)
if args.color_node:
    # Preserve the user's editable node; native baked materials remain the default.
    with bpy.data.libraries.load(str(Path(args.color_node).resolve()), link=False) as (source, target):
        if "ColorReplacementNode" not in source.node_groups:
            raise ValueError("ColorReplacementNode is missing from the chosen library")
        target.node_groups = ["ColorReplacementNode"]
    for group in target.node_groups:
        group.use_fake_user = True
    bpy.context.scene["trs_color_workflow"] = "Game colors are baked. ColorReplacementNode is available for optional manual recoloring."
destination.parent.mkdir(parents=True, exist_ok=True)
bpy.ops.wm.save_as_mainfile(filepath=str(destination))
print("TRS_RESULT:", destination, flush=True)
