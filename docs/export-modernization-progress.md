# Export modernization — validation record

Updated 2026-10-11. Task tag: [animations]. Branch: knight-animation-export. Draft PR: https://github.com/TorusXO/ThreeRingsSharp/pull/1.

## Delivered implementation

Qt/C++ 0.2.0 preview runs animation, selected archived level, and skybox-to-Blender pipelines with cancellable QProcess jobs, persistent paths, no-overwrite protection, diagnostic logs and batch mode. Original C# backend files are untouched. Legacy model browsing/export is launched in its original application; it has not been ported into Qt. New functions use an isolated native-game Java adapter and Blender 4.2 importer.

Native scene handling resolves tiles, placeables, actors, compound/conditional/schemed/static/articulated meshes and attachments. The old scene-global wrapper is migrated in memory from the existing repository XML for current game jars. Native texture colorization is baked; skybox model references retain their config arguments and camera-relative semantics. Additive sunbeams no longer become opaque dark cones. Runtime features are recorded as metadata; unresolved export errors stop the pipeline.

Blender optimization rule added to workspace AGENTS.md and this repository: reuse matching mesh data and image content, with object-linked materials for material variations. Optional supplied ColorReplacementNode loads in Blender4.2 and is appended locally (not linked) for manual editing. Native colorization remains the default.

Anatomical UE name mapping changes 27 names and all associated Action paths. Reference has 88 bones; the source retains49 nodes. This is not skeleton retargeting.

## Authoritative local checks

- C++ Release build: Qt6.11.2, MinGW13.1 x64. Java helper: javac21 --release11; installed-game runtime Java25. Blender4.2.9 LTS.
- Qt animation pipeline:150 Actions,49 nodes. validate_blender.py:21,609 source/evaluated pose comparisons, maximum error2.2053719e-6 after anatomical renaming.
- Qt Haven pipeline:19,126 source entries;29,414 mesh objects share669 mesh datablocks (802 source definitions,133 additional duplicates consolidated);163 packed/shared texture images. No missing images.29,633 independently reconstructed source placement checks, max1.72458e-6; Blender matrix error1.54972e-6.
- Saved Haven .blend reopened: same29,414 objects/669 meshes/163 packed images; ColorReplacementNode present, local, fake-user retained. One global skybox mesh;229 runtime metadata instances.
- Qt Clockwork skybox pipeline:25 source meshes,32 mesh instances,7 runtime markers, no missing images. Wrapper transforms exported without applying parallax as mesh scale. Recheck final packaged version after deployment.
- Scene regression passed: identical meshes share data despite material changes; transforms independent; distinct UVs and vertex colors retain separate meshes; two PNG files with different encoded bytes and identical pixels share one packed image.
- Qt failure regression passed: existing output preserved byte-for-byte, invalid scene stops pipeline, failed process launch stops pipeline, cancellation stops pipeline. All exit1 with no erroneous .blend output.
- Haven preview visually inspected after native recoloring and additive shader fix; main level layout retained, purple recolor masks and opaque sunbeam cones corrected. Lighting is an approximation, not an in-game screenshot match.

## Artifacts and handoff

Client target: E:/Unreal Projects/ThreeRingsSharp/releases/ThreeRingsStudio-0.2.0-preview/ThreeRingsStudio.exe. Build identity and SHA256 are in its sibling build-identity.json; final packaged hash is appended below after deployment.

Haven: E:/Unreal Projects/ThreeRingsSharp/scene-output/Haven-Town-Square/Level.blend.
Animations: E:/Unreal Projects/ThreeRingsSharp/animation-output/qt-knight/Knight-Animations.blend.
Skybox: E:/Unreal Projects/ThreeRingsSharp/scene-output/Clockwork-Skybox/Skybox.blend.

Test conditions: run the Qt client on this Windows PC with the provided game/Blender paths. Export animations, Haven selected from SKSA, and clockwork_skydome/skybox.dat to separate NEW output folders. Open each .blend in Blender4.2. Expected:150 Actions with anatomical names; Haven retains placement and shared data counts; skybox geometry and textures visible. Optional color group available under Add > Group. Existing output directory must be refused. Close/cancel cleanly; no interactive test waiting by the agent.

Return logs from %LOCALAPPDATA%/ThreeRingsSharp/ThreeRingsStudio/logs/export-*.log and the output folder's export-report.json or scene-report.json plus blender-scene-validation.json. Include selected source, Blender version, exact reproduction steps and screenshot for visual discrepancies. Logs include build identity. Future user-supplied logs follow the workspace's evidence-summary/model-selection workflow.

Known limits: original model UI still separate; Blender4.2 required; eight sequential/procedural animation configs unsupported; FBX weapon-helper roundtrip discrepancy; no full UE skeleton retarget; runtime particles, camera-follow parallax, fog, projected shadows, light sets, animated shader/atlas scrolling and GUI billboards are not recreated as working Blender effects. These26 Haven feature categories remain reported metadata. Native adapter reflection names target this installed game version. Manual tileset .blends are not required by native export.

External state: Z: drive mapping was unavailable, but \\192.168.0.252\Storage is reachable. Haven source cached in ignored .local/haven_town_square_origin. User assets, game jars/runtime, generated binaries and exports stay out of Git. No original game files modified.

## Final packaged client — 2026-10-11

Built from clean source commit 4e191314498072749e39163d33db7b465115ca3e, version0.2.0-preview, Release / Qt6.11.2 / MinGW x64, UTC2026-10-11T09:37:57Z.

Executable SHA256: 20121E6A6FF3878ABDABDB6422D8389209645013CE5DCA7C86AB951C2A0CE495.
Backend jar SHA256: AA3FE298F53AE18300F070BF88821A0DC8A86BBF9AEACE937EBE33D76923DB07.
Haven .blend SHA256: 24CF2F8022597A0BFDE086B29F95D840460BAC461EAD7333D7A8B15CBA403088.
Animation .blend SHA256: 924B96B7FFDD2C563E6223DC88922EA3CF2586912E0111F966D9900C74678821.
Skybox .blend SHA256: DFE63790143641706E9D8901E52EBAA1B2E17E901FC6E3BA0D5F2534E33DD015.

Packaged client was tested from its deployment directory, with no Java override: automatic sanitized runtime preparation succeeded, then Clockwork skybox export and Blender import exited0. Final skybox:32 mesh objects share21 meshes, one packed image, seven runtime metadata objects, five runtime-feature limitations, zero missing images. Qt screenshot verified readable controls and prefilled machine-local paths. paths.json is a local deployment file and is not committed.

Final saved Haven file reopened successfully and confirmed669 shared mesh datablocks/163 packed images, optional node group local and self-contained. Blender reports a benign UNC relative-path remapping warning for the appended library's origin; the group itself is local (library=None), verified after save/reopen. Runtime limitations remain as documented above. No interactive user testing has been assumed. No builds/export processes remain running at handoff.
