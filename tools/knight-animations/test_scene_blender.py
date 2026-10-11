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
    def png_chunk(kind, payload):
        body = kind + payload
        return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body))
    pixels = bytes([64, 128, 191, 0] + [64, 128, 191, 255] * 3)
    rows = b"\0" + pixels[:8] + b"\0" + pixels[8:]
    png = (b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 2, 8, 6, 0, 0, 0))
           + png_chunk(b"IDAT", zlib.compress(rows)) + png_chunk(b"IEND", b""))
    (root / "first.png").write_bytes(png)
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
    instances.append(dict(entry=4, type="PlaceableEntry", model="editor/marker/monster_wave/model.dat",
                          reference="fixture", mesh=0, matrix=identity, editorOnly=True))
    instances.append(dict(entry=5, type="PlaceableEntry", model="editor/marker/monster_wave/model.dat",
                          reference="fixture", mesh=None, matrix=identity, editorOnly=True, kind="wave"))
    data = dict(format="trs-scene-1", name="Regression fixture", resourceDirectory=str(root),
                materials={"a":dict(name="a",textures=["first.png"],parameters={},alphaMode="MASKED",alphaThreshold=0.92),
                           "b":dict(name="b",textures=["second.png"],parameters={"Color State Override":[0.5,1,1,1]})},
                meshes=meshes, instances=instances, metadata=[])
    path = root / "scene.json"
    path.write_text(json.dumps(data))
    collection = import_scene(path)
    objects = sorted([o for o in collection.all_objects if o.type == "MESH"], key=lambda o:o["trs_instance"])
    assert objects[0].data == objects[1].data, "Identical geometry with different materials must share a mesh"
    assert objects[0].material_slots[0].material != objects[1].material_slots[0].material
    assert len({o.data.as_pointer() for o in objects}) == 3, "UV and vertex-color differences must stay independent"
    assert [round(o.location.x) for o in objects] == [0, 2, 4, 6, 0]
    images = [next(n.image for n in o.material_slots[0].material.node_tree.nodes if n.type == "TEX_IMAGE") for o in objects]
    assert all(image == images[0] for image in images), "Identical pixels in differently encoded files must share an image"
    assert images[0].packed_file is not None
    masked = objects[0].material_slots[0].material
    alpha_link = masked.node_tree.nodes["Principled BSDF"].inputs["Alpha"].links[0]
    assert alpha_link.from_node.operation == "GREATER_THAN"
    assert abs(alpha_link.from_node.inputs[1].default_value - 0.92) < 1e-6
    assert masked.surface_render_method == "DITHERED"
    markers = next(c for c in collection.children if c.name == "Editor markers")
    assert not markers.hide_viewport and markers.hide_render
    assert objects[4].name in markers.objects
    assert len(markers.objects) == 2 and any(o.type == "EMPTY" for o in markers.objects)
    report = json.loads((root / "blender-scene-validation.json").read_text())
    assert report["unique_meshes"] == 3 and report["unique_texture_images"] == 1
    assert report["editor_marker_instances"] == 2 and report["editor_markers_visible"]
    assert report["editor_marker_mesh_instances"] == 1 and report["editor_marker_location_instances"] == 1
    assert report["alpha_bearing_opaque_materials"][0]["name"] == "b", (report["alpha_bearing_opaque_materials"], list(images[0].pixels))
print("TRS_SCENE_REGRESSION: PASS — material/alpha, visible markers, transforms, UV/color isolation, image reuse")
