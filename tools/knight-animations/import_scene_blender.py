"""Import static TRS scene geometry, linked mesh instances, and runtime metadata.

blender --background --python import_scene_blender.py -- scene.json output.blend
Use --python-exit-code 1 in automated runs. Existing scene objects are preserved.
"""
import json
import hashlib
from array import array
from collections import Counter
import math
import sys
from pathlib import Path
import bpy
from mathutils import Matrix, Vector


def import_scene(path):
    path = Path(path).resolve()
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("format") != "trs-scene-1":
        raise ValueError("Unsupported scene bundle")
    root = bpy.data.collections.new(data["name"])
    bpy.context.scene.collection.children.link(root)
    groups = {}
    for name in ("Tiles", "Props", "Skyboxes", "Editor markers", "Runtime metadata"):
        group = bpy.data.collections.new(name)
        root.children.link(group)
        groups[name] = group
    groups["Editor markers"].hide_render = True
    groups["Runtime metadata"].hide_render = True
    groups["Runtime metadata"].hide_viewport = True
    resources = Path(data["resourceDirectory"])
    missing_images = []
    materials = {}
    image_cache = {}
    image_file_cache = {}
    image_has_alpha = {}
    opaque_alpha_materials = set()
    for key, source in data["materials"].items():
        material = bpy.data.materials.new(source["name"] or "Untextured")
        material.use_nodes = True
        material["trs_reference"] = source.get("reference") or ""
        material["trs_parameters"] = json.dumps(source.get("parameters", {}))
        alpha_mode = source.get("alphaMode") or next((mode for word, mode in (("Masked", "MASKED"), ("Translucent", "TRANSLUCENT"), ("Additive", "ADDITIVE")) if word in key), "OPAQUE")
        material["trs_alpha_mode"] = alpha_mode
        material["trs_alpha_threshold"] = source.get("alphaThreshold", 0.5)
        material["trs_overlay_mode"] = source.get("overlayMode") or ""
        nodes, links = material.node_tree.nodes, material.node_tree.links
        bsdf = nodes.get("Principled BSDF")
        bsdf.inputs["Roughness"].default_value = 1
        bsdf.inputs["Specular IOR Level"].default_value = 0
        textures = source["textures"]
        params = source.get("parameters", {})
        color_output = None
        alpha_output = None
        if textures:
            image_path = path.parent / textures[0][8:] if textures[0].startswith("@bundle/") else resources / textures[0]
            if image_path.is_file():
                # All diffuse sources here use sRGB. Different interpretations must
                # have different keys when non-color texture channels are added.
                image_key = (hashlib.sha256(image_path.read_bytes()).hexdigest(), "sRGB")
                image = image_file_cache.get(image_key)
                if image is None:
                    image = bpy.data.images.load(str(image_path), check_existing=True)
                    image.colorspace_settings.name = "sRGB"
                    pixels = array("f", [0]) * len(image.pixels)
                    image.pixels.foreach_get(pixels)
                    contains_alpha = image.channels == 4 and any(value < 0.999 for value in pixels[3::4])
                    pixel_key = (tuple(image.size), image.channels, "sRGB", hashlib.sha256(pixels.tobytes()).hexdigest())
                    existing = image_cache.get(pixel_key)
                    if existing is not None and existing != image:
                        if image.users == 0:
                            bpy.data.images.remove(image)
                        image = existing
                    image_cache[pixel_key] = image
                    image_file_cache[image_key] = image
                    image_has_alpha[image.as_pointer()] = contains_alpha
                if alpha_mode == "OPAQUE" and image_has_alpha.get(image.as_pointer(), False):
                    opaque_alpha_materials.add(key)
                if image.packed_file is None:
                    image.pack()
                tex = nodes.new("ShaderNodeTexImage")
                tex.image = image
                tex.interpolation = "Closest" if "NEAREST" in key else "Linear"
                color_output, alpha_output = tex.outputs["Color"], tex.outputs["Alpha"]
                # Game animation atlases start with one cell; dynamic shader animation
                # is not baked by this static scene importer.
                params = source.get("parameters", {})
                if "Divisions S" in params or "Divisions T" in params:
                    uv = nodes.new("ShaderNodeTexCoord")
                    scale = nodes.new("ShaderNodeVectorMath")
                    scale.operation = "MULTIPLY"
                    scale.inputs[1].default_value = (1/(2**params.get("Divisions S", 0)), 1/(2**params.get("Divisions T", 0)), 1)
                    links.new(uv.outputs["UV"], scale.inputs[0])
                    links.new(scale.outputs[0], tex.inputs["Vector"])
            else:
                missing_images.append(str(image_path))
        tint = params.get("Color State Override", [1, 1, 1, 1])
        vertex = nodes.new("ShaderNodeVertexColor")
        vertex.layer_name = "ClydeColor"
        multiply = nodes.new("ShaderNodeMixRGB")
        multiply.blend_type = "MULTIPLY"
        multiply.inputs[0].default_value = 1
        multiply.inputs[1].default_value = tint
        links.new(vertex.outputs["Color"], multiply.inputs[2])
        if color_output:
            texture_multiply = nodes.new("ShaderNodeMixRGB")
            texture_multiply.blend_type = "MULTIPLY"
            texture_multiply.inputs[0].default_value = 1
            links.new(color_output, texture_multiply.inputs[1])
            links.new(multiply.outputs[0], texture_multiply.inputs[2])
            color_output = texture_multiply.outputs[0]
        else:
            color_output = multiply.outputs[0]
        links.new(color_output, bsdf.inputs["Base Color"])
        alpha = nodes.new("ShaderNodeMath")
        alpha.operation = "MULTIPLY"
        alpha.inputs[1].default_value = tint[3]
        links.new(vertex.outputs["Alpha"], alpha.inputs[0])
        if alpha_output:
            texture_alpha = nodes.new("ShaderNodeMath")
            texture_alpha.operation = "MULTIPLY"
            links.new(alpha_output, texture_alpha.inputs[0])
            links.new(alpha.outputs[0], texture_alpha.inputs[1])
            alpha = texture_alpha
        if alpha_mode == "MASKED":
            cutoff = nodes.new("ShaderNodeMath")
            cutoff.operation = "GREATER_THAN"
            cutoff.inputs[1].default_value = float(source.get("alphaThreshold", 0.5))
            links.new(alpha.outputs[0], cutoff.inputs[0])
            links.new(cutoff.outputs[0], bsdf.inputs["Alpha"])
            material.surface_render_method = "DITHERED"
        elif alpha_mode == "TRANSLUCENT":
            links.new(alpha.outputs[0], bsdf.inputs["Alpha"])
            material.surface_render_method = "DITHERED"
        if alpha_mode == "ADDITIVE":
            # Additive meshes (sunbeams, glows) must not become opaque black cones.
            emission = nodes.new("ShaderNodeEmission")
            links.new(color_output, emission.inputs["Color"])
            links.new(alpha.outputs[0], emission.inputs["Strength"])
            transparent = nodes.new("ShaderNodeBsdfTransparent")
            add = nodes.new("ShaderNodeAddShader")
            links.new(transparent.outputs[0], add.inputs[0])
            links.new(emission.outputs[0], add.inputs[1])
            links.new(add.outputs[0], nodes.get("Material Output").inputs["Surface"])
        materials[key] = material
    meshes = []
    mesh_cache = {}
    redundant_faces = 0
    for number, source in enumerate(data["meshes"]):
        # Materials belong to objects, so identical geometry can share storage
        # even when two placements use different recolors or surface materials.
        geometry_key = hashlib.sha256(json.dumps(
            {k: source.get(k) for k in ("positions", "normals", "uv", "colors", "faces")},
            sort_keys=True, separators=(",", ":")).encode()).hexdigest()
        if geometry_key in mesh_cache:
            meshes.append(mesh_cache[geometry_key])
            continue
        mesh = bpy.data.meshes.new(f"{number}: {source['name']}")
        # Some source shields duplicate every triangle with reverse winding.
        # Blender represents these using a double-sided material, not duplicate faces.
        faces, seen = [], set()
        for face in source["faces"]:
            key = tuple(sorted(face))
            if len(set(face)) < 3 or key in seen:
                redundant_faces += 1
                continue
            seen.add(key)
            faces.append(face)
        mesh.from_pydata(source["positions"], [], faces)
        if mesh.validate(verbose=False):
            raise ValueError(f"Invalid source topology required repair: mesh {number}")
        mesh.update()
        mesh.materials.append(materials[source["material"]])
        if source["uv"]:
            uv = mesh.uv_layers.new(name="UVMap")
            uv.data.foreach_set("uv", [component for loop in mesh.loops for component in source["uv"][loop.vertex_index][:2]])
        if source["normals"]:
            for polygon in mesh.polygons:
                polygon.use_smooth = True
            mesh.normals_split_custom_set_from_vertices(source["normals"])
        attr = mesh.color_attributes.new(name="ClydeColor", type="FLOAT_COLOR", domain="POINT")
        if source.get("colors"):
            colors = [list(c[:4]) if len(c) >= 4 else list(c[:3])+[1] for c in source["colors"]]
            attr.data.foreach_set("color", [v for c in colors for v in c])
        else:
            attr.data.foreach_set("color", [1.0] * (4 * len(mesh.vertices)))
        mesh["trs_mesh_index"] = number
        mesh_cache[geometry_key] = mesh
        meshes.append(mesh)
    objects = []
    sky_materials = {}
    placement_checks = 0
    placement_error = 0.0
    expected = []
    for i, instance in enumerate(data["instances"]):
        placement = data.get("placements", {}).get(str(instance["entry"]))
        if placement and "localMatrix" in instance and not instance.get("skybox"):
            if placement["type"] == "TileEntry":
                # Independently reconstruct the tile pivot from raw grid coordinates;
                # catches accidentally calling the engine's transform MUTATOR overload.
                rotation = placement["rotation"]
                base = Matrix.Rotation(rotation * math.pi/2, 4, "Z")
                base.translation = (placement["x"] + (placement["height"] if rotation == 1 else placement["width"] if rotation == 2 else 0),
                                    placement["y"] + (placement["height"] if rotation == 2 else placement["width"] if rotation == 3 else 0),
                                    placement["elevation"] * placement["elevationScale"])
            else:
                base = Matrix(placement["matrix"])
            native = base @ Matrix(instance["localMatrix"])
            error = max(abs(native[r][c]-instance["matrix"][r][c]) for r in range(4) for c in range(4))
            placement_error = max(placement_error, error)
            placement_checks += 1
            if error > 0.001:
                raise ValueError(f"Source placement mismatch for entry {instance['entry']}: {error}")
        mesh_id = instance["mesh"]
        obj = bpy.data.objects.new(f"{instance['entry']}: {Path(instance.get('detail') or instance['model']).stem}", meshes[mesh_id] if mesh_id is not None else None)
        if mesh_id is None:
            obj.empty_display_type = "SPHERE" if instance.get("editorOnly") else "PLAIN_AXES"
            obj.empty_display_size = 0.5 if instance.get("editorOnly") else 0.2
            group = groups["Editor markers"] if instance.get("editorOnly") else groups["Runtime metadata"]
            obj["trs_runtime_kind"] = instance.get("kind") or ""
            obj["trs_configuration"] = json.dumps(instance.get("configuration"))
        else:
            obj.material_slots[0].link = "OBJECT"
            obj.material_slots[0].material = materials[data["meshes"][mesh_id]["material"]]
            group = groups["Skyboxes"] if instance.get("skybox") else groups["Editor markers"] if instance.get("editorOnly") else groups["Tiles"] if instance["type"] == "TileEntry" else groups["Props"]
        if instance.get("editorOnly"):
            obj["trs_editor_marker"] = True
        if instance.get("skybox"):
            obj["trs_skybox"] = True
            obj["trs_camera_parallax"] = json.dumps(instance.get("configuration"))
            if mesh_id is not None:
                obj.visible_shadow = False
                key = data["meshes"][mesh_id]["material"]
                if (data["materials"][key].get("alphaMode") or ("ADDITIVE" if "Additive" in key else "OPAQUE")) != "ADDITIVE":
                    if key not in sky_materials:
                        material = materials[key].copy()
                        material.name += " · Sky unlit"
                        nodes, links = material.node_tree.nodes, material.node_tree.links
                        bsdf = nodes.get("Principled BSDF")
                        emission = nodes.new("ShaderNodeEmission")
                        links.new(bsdf.inputs["Base Color"].links[0].from_socket, emission.inputs["Color"])
                        transparent = nodes.new("ShaderNodeBsdfTransparent")
                        mix = nodes.new("ShaderNodeMixShader")
                        links.new(transparent.outputs[0], mix.inputs[1])
                        links.new(emission.outputs[0], mix.inputs[2])
                        if bsdf.inputs["Alpha"].is_linked:
                            links.new(bsdf.inputs["Alpha"].links[0].from_socket, mix.inputs[0])
                        else:
                            mix.inputs[0].default_value = bsdf.inputs["Alpha"].default_value
                        links.new(mix.outputs[0], nodes.get("Material Output").inputs["Surface"])
                        sky_materials[key] = material
                    obj.material_slots[0].material = sky_materials[key]
        group.objects.link(obj)
        transform = Matrix(instance["matrix"])
        obj.matrix_world = transform
        obj["trs_instance"] = i
        obj["trs_entry"] = instance["entry"]
        obj["trs_model"] = instance["model"]
        obj["trs_reference"] = instance["reference"]
        objects.append(obj)
        expected.append(transform)
    for source in data["metadata"]:
        obj = bpy.data.objects.new(f"Metadata {source['id']}: {source['type']}", None)
        obj["trs_metadata"] = json.dumps(source)
        obj["trs_entry"] = source["id"]
        groups["Runtime metadata"].objects.link(obj)
        if "matrix" in source:
            obj.matrix_world = Matrix(source["matrix"])
    bpy.context.view_layer.update()
    max_error = 0.0
    for obj, target in zip(objects, expected):
        max_error = max(max_error, max(abs(obj.matrix_world[r][c]-target[r][c]) for r in range(4) for c in range(4)))
    if max_error > 0.001:
        raise ValueError(f"Scene transform error {max_error}; object hierarchy may require shear preservation")
    if missing_images:
        raise FileNotFoundError("Missing textures: " + ", ".join(missing_images))
    visible_material_uses = Counter(
        data["meshes"][instance["mesh"]]["material"]
        for instance in data["instances"]
        if instance["mesh"] is not None and not instance.get("skybox")
        and not instance.get("editorOnly")
    )
    untextured_visible_materials = [
        {"name": data["materials"][key]["name"],
         "reference": data["materials"][key]["reference"],
         "mesh_instances": uses}
        for key, uses in sorted(visible_material_uses.items())
        if not data["materials"][key]["textures"]
    ]
    visible = [o for o in objects if o.type == "MESH" and not o.get("trs_skybox") and not o.get("trs_editor_marker") and not any(c.hide_viewport for c in o.users_collection)]
    points = [o.matrix_world @ Vector(p) for o in visible for p in o.bound_box]
    if points:
        lo = Vector([min(p[i] for p in points) for i in range(3)])
        hi = Vector([max(p[i] for p in points) for i in range(3)])
        for screen in bpy.data.screens:
            for area in screen.areas:
                if area.type == "VIEW_3D":
                    area.spaces.active.clip_end = 10000
                    area.spaces.active.region_3d.view_location = (lo+hi)/2
                    area.spaces.active.region_3d.view_distance = max((hi-lo).length, 1)
    root["trs_bundle"] = str(path)
    root["trs_max_matrix_error"] = max_error
    root["trs_entry_metadata"] = json.dumps(data["metadata"])
    root["trs_editor_marker_instances"] = sum(bool(o.get("trs_editor_marker")) for o in objects)
    alpha_bearing_opaque_materials = [
        {"name": data["materials"][key]["name"], "reference": data["materials"][key].get("reference"), "mesh_instances": uses}
        for key, uses in sorted(visible_material_uses.items()) if key in opaque_alpha_materials
    ]
    unrendered_overlay_materials = [
        {"name": data["materials"][key]["name"], "reference": data["materials"][key].get("reference"),
         "overlay_mode": data["materials"][key]["overlayMode"], "mesh_instances": uses}
        for key, uses in sorted(visible_material_uses.items()) if data["materials"][key].get("overlayMode")
    ]
    report = dict(mesh_definitions=len(meshes), unique_meshes=len(mesh_cache), reused_mesh_definitions=len(meshes)-len(mesh_cache), unique_texture_images=len(image_cache), mesh_instances=sum(o.type == "MESH" for o in objects),
                  source_placement_checks=placement_checks, maximum_source_placement_error=placement_error,
                  runtime_instances=sum(o.type == "EMPTY" for o in objects), materials=len(materials),
                  packed_images=sum(image.packed_file is not None for image in bpy.data.images),
                  redundant_or_degenerate_faces_removed=redundant_faces,
                  maximum_transform_error=max_error, missing_images=missing_images,
                  untextured_visible_materials=untextured_visible_materials,
                  editor_marker_instances=root["trs_editor_marker_instances"],
                  editor_marker_mesh_instances=sum(o.type == "MESH" and bool(o.get("trs_editor_marker")) for o in objects),
                  editor_marker_location_instances=sum(o.type == "EMPTY" and bool(o.get("trs_editor_marker")) for o in objects),
                  editor_markers_visible=not groups["Editor markers"].hide_viewport,
                  alpha_bearing_opaque_materials=alpha_bearing_opaque_materials,
                  unrendered_overlay_materials=unrendered_overlay_materials)
    (path.parent / "blender-scene-validation.json").write_text(json.dumps(report, indent=2))
    print("TRS_SCENE_IMPORT:", report)
    return root


if __name__ == "__main__":
    args = sys.argv[sys.argv.index("--")+1:]
    if len(args) != 2:
        raise SystemExit("Expected: -- scene.json output.blend")
    destination = Path(args[1]).resolve()
    if destination.exists():
        raise FileExistsError(destination)
    import_scene(args[0])
    destination.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(destination))
