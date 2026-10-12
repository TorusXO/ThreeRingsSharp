# ThreeRings Studio — Qt preview

Qt 6/C++ frontend for native-game model, animation, selected-level, and skybox export. The original C# / IKVM backend is unchanged. The Models tab brings path search, config inspection, static-set variant and Choice parameter selection, hierarchy inspection, and selected-model export into Qt.

## Run

Keep the portable application folder together, including `runtime/`, `tools/blender/`, Qt DLLs, `platforms/`, and `backend/`. Run `ThreeRingsStudio.exe`. The portable release includes Java 25 and Blender 4.2 LTS; no separate installation or compiler is needed.

Choose only the Spiral Knights installation, one reused output folder, and the folder containing saved scenes/levels. Advanced command-line overrides of Blender and Java paths remain for development and testing. An unbundled development build can also use the game's Java runtime, copying it into an app cache when obsolete `lib/ext` files would prevent startup.

- **Models:** scan or choose a game model `.dat`, filter paths, inspect the model hierarchy and source parameters, select a static-set variant or Choice values, then export a packed `.blend` and optionally a `.glb`. Preview in Blender exports the selected model and opens its `.blend`. The experimental SpiralView launcher uses the open-source viewer and bundled LWJGL 2 support; with the tested current game build it exits due to a viewer/game library mismatch. Its `<model prefix>.spiralview.log` and the game's `viewer.log` record startup errors. Model export currently includes visible static geometry and textures; articulated model rigs, skinning, animation tracks and effect systems are not yet preserved in model-mode `.glb` files. The dedicated Knight animations tab exports the supported animation tracks separately.
- **Knight animations:** exports all supported stored clips as Actions. Anatomical UE names are enabled by default, checked against the supplied reference rig. This preserves the 49-node SK hierarchy; it does not retarget onto the reference's 88-bone skeleton. Optional FBX export has a known weapon-helper discrepancy; use the `.blend` as the authoritative result.
- **Levels:** choose an SKSA archive directory, scan/filter it, select a scene, then export. Extensionless scene files and `.dat` files are listed; a non-scene file is rejected by the backend. The level list identifies numeric files and exact content matches against the supplied 205-name scene catalog. The Blender collection, reports and output filename use the recognized name; JSON also retains the scene's internal name, original path, catalog ID and match method. Unmatched scenes retain their source filename. The catalog is user-supplied archive data and may contain historical names or uncertainty notes.
- **Skyboxes:** scan the game's skybox directory or choose a wrapper/model `.dat`. Skybox geometry uses unlit materials, independent transforms, and recorded camera-parallax settings. Parallax scale is never applied as mesh scale.
The portable build carries the supplied `Color_Replacement_Node.blend` and appends `ColorReplacementNode` as a local editable group. It is available through the Shader Editor's Add → Group menu. The default materials already use native game recoloring; the optional group is not automatically inserted into their links.

Choose one output folder for any number of exports. Each level or skybox uses its source name plus a short path hash in every filename; knight animations have their own prefix. JSON data, reports, logs, FBX files, generated recolored textures and packed `.blend` files all live directly in that folder. Re-exporting the same source replaces its named files and log, while exports from other sources and unrelated files remain. Recolored PNGs use content hashes so identical textures can be reused. A temporary staging folder is removed after each run. If an export fails, its previous completed `.blend` remains available and its named log and `last-failed-report.json` explain the failure.

When a recognized name changes a source's prefix, a successful export removes that same source's older generic prefix files. The two catalog TSV files in `backend/` are needed for scene identification. They contain only names and hashes from the user's archived scene copies, not game assets.

The level importer uses linked mesh datablocks for identical geometry/UVs/normals/vertex colors. Materials are linked to objects so differently colored instances can share geometry. Textures are deduplicated by decoded pixel content plus color-space interpretation, including differently encoded files. The exporter follows nested material references to resolve diffuse images, including materials whose mesh labels use legacy `.psd` names. The validation report includes unique mesh/image counts, placement checks, and visible materials without resolved textures.

The exporter also follows inherited material references for masked, translucent and additive surface modes. Masked materials use their source alpha threshold, so transparent wall-panel cutouts show through in Blender. The validation report lists any visible material with an alpha-bearing image that still resolves as opaque. Stored development meshes, including monster spawn and wave markers, appear in the **Editor markers** collection, which is hidden from renders and can be hidden in the viewport. Other runtime-only definitions stay in **Runtime metadata** as hidden named objects; their full game behavior is not recreated.

Materials named `Sphere Overlay/Additive Mask` keep their ordinary textured base in Blender. The game's extra sphere-mapped additive sheen is recorded as material metadata and as a report limitation, because that overlay pass is not yet recreated. A scene-global entry with a null reference and no model is likewise retained as metadata instead of stopping extraction.

## Compatibility and scope

The native extension resolves configs using the locally installed game jars, without redistributing them. Reflection entrypoints currently target the tested installed game; an upstream obfuscation change can require adapting the adapter. No game assets or runtime are included in Git. The portable release bundles the independently redistributable Blender, Eclipse Temurin JRE, SpiralView and LWJGL 2 dependencies with their license notices; the user still supplies the installed game's own files. SpiralView's current released viewer remains incompatible with the tested game libraries, so Blender preview is the working model preview path.

The current installation lacks `rsrc/config/placeable.dat`. If present, the existing `placeable_projectx.dat` in the game directory is loaded as a fallback. Missing configs are errors, not successful placeholder geometry.

Old scene-global binary configs use a wrapper class that is abstract in current game jars. The shipped legacy XML (already present in the original repository) is migrated **in memory**, preserving model references and parameters. Select a different globals XML for another archive version if needed, or clear it to use the game binary config.

Some archived scenes use a parameter-only `Environment/Model` global with a model argument and no default implementation. The native adapter exports that supplied model, including skybox geometry, while retaining the reference and selected arguments in JSON.

Some archived Castle floor-edge tiles request an old `edge_thin_corner_Out_02` static-set key. If the installed game model contains `edge_corner_out_02`, that available geometry is used and recorded as a limitation in the scene report. The result is an approximation of the archived variant.

Clockwork Tutorial Depth 1 uses an old `Dynamic/Block/Treasure/Clockwork Tunnels` placeable absent from both the saved and installed placeable catalogs. When the game returns its error placeholder for exactly that reference, the exporter uses the current game's normal treasure-block model with its Green variant, as defined in `placeable_tudey.xml`. The original placeable reference stays in instance metadata and the substitution is recorded as a limitation. Its historic variant and gameplay behavior are not reconstructed.

Static geometry, native recoloring, vertex colors, alpha/additive materials and skybox geometry are implemented. Particle systems, procedural effects, GUI billboards, animated texture scrolling/atlases, engine fog, light sets, projected shadows and camera-follow behavior are not a complete Blender recreation of the game renderer. Unsupported runtime features remain named metadata objects and are reported as limitations. Resolution/geometry/material errors stop the pipeline. The game-specific shader conversions are approximations, not a pixel-identical renderer.

## Build and validate

`build.ps1` defaults to the installed Qt 6.11.2 MinGW kit, CMake, Ninja and JDK 21. It builds C++ Release, compiles the extension to Java 11 bytecode, deploys Qt and packages Python scripts and compatibility XML. `package-portable.ps1` then copies Blender 4.2 LTS, a Java 25 JRE, SpiralView, LWJGL 2 jars/native DLLs, and the color node into the deploy folder. It can produce a ZIP. Use upstream redistributable downloads and retain the license notices; do not add binaries or game jars to Git.

```powershell
./qt/build.ps1 -Destination ./releases/ThreeRingsStudio-0.3.0-preview
./qt/package-portable.ps1 -ClientDirectory ./releases/ThreeRingsStudio-0.3.0-preview -BlenderDirectory C:/path/to/blender-4.2.9-windows-x64 -JavaRuntimeDirectory C:/path/to/jdk-25-jre -SpiralViewJar C:/path/to/spiralview.jar -Lwjgl2Directory C:/path/to/lwjgl2 -ColorNodeBlend 'C:/path/to/Color_Replacement_Node.blend' -CreateZip
```

Override paths with `-QtDirectory`, `-QtToolsDirectory`, `-JdkDirectory`, `-BuildDirectory`, `-Destination`. Deployment writes `build-identity.json` with executable/backend hashes and source/build identity. Qt is dynamically linked; deployed license texts remain alongside the executable. The build does not compile or alter the legacy C# applications.

The same QProcess pipeline supports batch validation:

```powershell
./ThreeRingsStudio.exe --batch scene --game 'C:/Games/Spiral Knights' --source 'C:/SKSA/scenes/haven_town_square_origin' --blender 'C:/Blender/blender.exe' --output 'C:/Exports/TRS'
```

`--batch animation|model|scene|skybox`; optional `--java`, `--blender`, `--globals`, `--source-names`, `--fbx`, `--color-node`, `--glb`, `--model-variant`. Exit 0 means a `.blend` was created, with any reported limitations; exit 1 means failure/cancellation. `--screenshot` captures the Qt window for local layout inspection. `--cancel-after-ms` exercises cancellation in batch regression checks. UI folder preferences persist in QSettings; an optional local `paths.json` supplies initial defaults for a particular machine.

`test-client.ps1` checks failure handling in a reusable folder, invalid scenes, failed process startup and cancellation. Run `test_scene_blender.py` with Blender `--background --factory-startup --python-exit-code 1 --python` for mesh/material/UV/color/image deduplication regression checks. `validate_blender.py` checks animation poses against source matrices, including renamed rigs.

Logs: `<export folder>/<source prefix>.export.log`. Each log includes build identity, selected source/output and full backend output. Level and skybox exports include `<prefix>.scene-report.json` and `<prefix>.blender-scene-validation.json`; animations include `<prefix>.export-report.json`.
