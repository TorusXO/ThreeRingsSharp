# ThreeRings Studio — Qt preview

Qt 6/C++ frontend for the new native-game animation and scene export extensions. The original C# / IKVM backend is unchanged. **Open original model exporter** launches the existing SKAnimatorTools installation; its old model browsing/export interface has not been ported into Qt. Animation, selected archived level, and skybox export run directly from the new Qt interface.

## Run

Keep the deployed application folder together, including Qt DLLs, `platforms/` and `backend/`. Run `ThreeRingsStudio.exe`. No compiler is needed to use a packaged build.

Set the game installation and a **Blender 4.2 LTS** executable. Leave Java blank to use the installed game runtime. If an upgraded runtime contains obsolete Java 8 `lib/ext` files, the app copies the runtime into its own cache excluding those files; the game installation is never modified. A compatible Java executable can also be selected explicitly.

- **Knight animations:** exports all supported stored clips as Actions. Anatomical UE names are enabled by default, checked against the supplied reference rig. This preserves the 49-node SK hierarchy; it does not retarget onto the reference's 88-bone skeleton. Optional FBX export has a known weapon-helper discrepancy; use the `.blend` as the authoritative result.
- **Levels:** choose an SKSA archive directory, scan/filter it, select a scene, then export. Extensionless scene files and `.dat` files are listed; a non-scene file is rejected by the backend. The level list identifies numeric files and exact content matches against the supplied 205-name scene catalog. The Blender collection, reports and output filename use the recognized name; JSON also retains the scene's internal name, original path, catalog ID and match method. Unmatched scenes retain their source filename. The catalog is user-supplied archive data and may contain historical names or uncertainty notes.
- **Skyboxes:** scan the game's skybox directory or choose a wrapper/model `.dat`. Skybox geometry uses unlit materials, independent transforms, and recorded camera-parallax settings. Parallax scale is never applied as mesh scale.
- **Optional color node:** select the user's `Color_Replacement_Node.blend` to append `ColorReplacementNode` as a local editable group. It is available through the Shader Editor's Add → Group menu. The default materials already use native game recoloring; the optional group is not automatically inserted into their links.

Choose one output folder for any number of exports. Each level or skybox uses its source name plus a short path hash in every filename; knight animations have their own prefix. JSON data, reports, logs, FBX files, generated recolored textures and packed `.blend` files all live directly in that folder. Re-exporting the same source replaces its named files and log, while exports from other sources and unrelated files remain. Recolored PNGs use content hashes so identical textures can be reused. A temporary staging folder is removed after each run. If an export fails, its previous completed `.blend` remains available and its named log and `last-failed-report.json` explain the failure.

When a recognized name changes a source's prefix, a successful export removes that same source's older generic prefix files. The two catalog TSV files in `backend/` are needed for scene identification. They contain only names and hashes from the user's archived scene copies, not game assets.

The level importer uses linked mesh datablocks for identical geometry/UVs/normals/vertex colors. Materials are linked to objects so differently colored instances can share geometry. Textures are deduplicated by decoded pixel content plus color-space interpretation, including differently encoded files. The exporter follows nested material references to resolve diffuse images, including materials whose mesh labels use legacy `.psd` names. The validation report includes unique mesh/image counts, placement checks, and visible materials without resolved textures.

## Compatibility and scope

The native extension resolves configs using the locally installed game jars, without redistributing them. Reflection entrypoints currently target the tested installed game; an upstream obfuscation change can require adapting the adapter. No game assets or runtime are included in Git.

The current installation lacks `rsrc/config/placeable.dat`. If present, the existing `placeable_projectx.dat` in the game directory is loaded as a fallback. Missing configs are errors, not successful placeholder geometry.

Old scene-global binary configs use a wrapper class that is abstract in current game jars. The shipped legacy XML (already present in the original repository) is migrated **in memory**, preserving model references and parameters. Select a different globals XML for another archive version if needed, or clear it to use the game binary config.

Some archived scenes use a parameter-only `Environment/Model` global with a model argument and no default implementation. The native adapter exports that supplied model, including skybox geometry, while retaining the reference and selected arguments in JSON.

Some archived Castle floor-edge tiles request an old `edge_thin_corner_Out_02` static-set key. If the installed game model contains `edge_corner_out_02`, that available geometry is used and recorded as a limitation in the scene report. The result is an approximation of the archived variant.

Static geometry, native recoloring, vertex colors, alpha/additive materials and skybox geometry are implemented. Particle systems, procedural effects, GUI billboards, animated texture scrolling/atlases, engine fog, light sets, projected shadows and camera-follow behavior are not a complete Blender recreation of the game renderer. Unsupported runtime features remain named metadata objects and are reported as limitations. Resolution/geometry/material errors stop the pipeline. The game-specific shader conversions are approximations, not a pixel-identical renderer.

## Build and validate

`build.ps1` defaults to the installed Qt 6.11.2 MinGW kit, CMake, Ninja and JDK 21. It builds C++ Release, compiles the extension to Java 11 bytecode, deploys Qt and packages Python scripts and compatibility XML. The Java **runtime** must still support the installed game jars (Java 25 for the tested installation).

```powershell
./qt/build.ps1 -Destination ./releases/ThreeRingsStudio-0.2.3-preview
```

Override paths with `-QtDirectory`, `-QtToolsDirectory`, `-JdkDirectory`, `-BuildDirectory`, `-Destination`. Deployment writes `build-identity.json` with executable/backend hashes and source/build identity. Qt is dynamically linked; deployed license texts remain alongside the executable. The build does not compile or alter the legacy C# applications.

The same QProcess pipeline supports batch validation:

```powershell
./ThreeRingsStudio.exe --batch scene --game 'C:/Games/Spiral Knights' --source 'C:/SKSA/scenes/haven_town_square_origin' --blender 'C:/Blender/blender.exe' --output 'C:/Exports/TRS'
```

`--batch animation|scene|skybox`; optional `--java`, `--globals`, `--source-names`, `--fbx`, `--color-node`. Exit 0 means a `.blend` was created, with any reported limitations; exit 1 means failure/cancellation. `--screenshot` captures the Qt window for local layout inspection. `--cancel-after-ms` exercises cancellation in batch regression checks. UI path preferences persist in QSettings; an optional local `paths.json` supplies initial defaults for a particular machine.

`test-client.ps1` checks failure handling in a reusable folder, invalid scenes, failed process startup and cancellation. Run `test_scene_blender.py` with Blender `--background --factory-startup --python-exit-code 1 --python` for mesh/material/UV/color/image deduplication regression checks. `validate_blender.py` checks animation poses against source matrices, including renamed rigs.

Logs: `<export folder>/<source prefix>.export.log`. Each log includes build identity, selected source/output and full backend output. Level and skybox exports include `<prefix>.scene-report.json` and `<prefix>.blender-scene-validation.json`; animations include `<prefix>.export-report.json`.
