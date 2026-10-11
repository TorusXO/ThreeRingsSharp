"""Blender 4.2 regression: instancing, per-object materials and image content reuse."""
import copy
import json
import struct
import sys
import tempfile
import zlib
from pathlib import Path

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
from import_scene_blender import import_scene

with tempfile.TemporaryDirectory(prefix="trs-scene-test-") as directory:
    root = Path(directory)
    image = bpy.data.images.new("fixture", width=2, height=2)
    image.pixels[:] = [0.25, 0.5, 0.75, 1.0] * 4
    image.filepath_raw = str(root / "first.png")
    image.file_format = "PNG"
    image.save()
    bpy.data.images.remove(image)
    png = (root / "first.png").read_bytes()
    # Same pixels, different bytes: an ancillary PNG text chunk changes the hash.
    payload = b"Source\0second file"
    chunk = b"tEXt" + payload
    extra = struct.pack(">I", len(payload)) + chunk + struct.pack(">I", zlib.crc32(chunk))
    (root / "second.png").write_bytes(png[:-12] + extra + png[-12:])
    identity = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]
    mesh = dict(name="triangle", positions=[[0,0,0],[1,0,0],[0,1,0]], normals=[[0,0,1]]*3,
                uv=[[0,0],[1,0],[0,1]], colors=None, faces=[[0,1,2]], material="a")
    meshes = [copy.deepcopy(mesh) for _ in range(4)]
    meshes[1]["material"] = "b"
    meshes[2]["uv"][0] = [0.25, 0.25]
    meshes[3]["colors"] = [[1,0,0,1]]*3
    instances = []
    for i in range(4):
        transform = copy.deepcopy(identity)
        transform[0][3] = i*2
        instances.append(dict(entry=i, type="PlaceableEntry", model="fixture", reference="fixture", mesh=i, matrix=transform))
    data = dict(format="trs-scene-1", name="Regression fixture", resourceDirectory=str(root),
                materials={"a":dict(name="a",textures=["first.png"],parameters={}),
                           "b":dict(name="b",textures=["second.png"],parameters={"Color State Override":[0.5,1,1,1]})},
                meshes=meshes, instances=instances, metadata=[])
    path = root / "scene.json"
    path.write_text(json.dumps(data))
    collection = import_scene(path)
    objects = sorted([o for o in collection.all_objects if o.type == "MESH"], key=lambda o:o["trs_instance"])
    assert objects[0].data == objects[1].data, "Identical geometry with different materials must share a mesh"
    assert objects[0].material_slots[0].material != objects[1].material_slots[0].material
    assert len({o.data.as_pointer() for o in objects}) == 3, "UV and vertex-color differences must stay independent"
    assert [round(o.location.x) for o in objects] == [0, 2, 4, 6]
    images = [next(n.image for n in o.material_slots[0].material.node_tree.nodes if n.type == "TEX_IMAGE") for o in objects]
    assert all(image == images[0] for image in images), "Identical pixels in differently encoded files must share an image"
    assert images[0].packed_file is not None
    report = json.loads((root / "blender-scene-validation.json").read_text())
    assert report["unique_meshes"] == 3 and report["unique_texture_images"] == 1
print("TRS_SCENE_REGRESSION: PASS — material overrides, transforms, UV/color isolation, pixel-content image reuse")
