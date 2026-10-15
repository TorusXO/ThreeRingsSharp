# ThreeRingsSharp / ThreeRings Studio

ThreeRings Studio is a Windows Qt 6/C++ application for exporting Spiral Knights models, player-knight animations, archived levels, and skyboxes into Blender. It includes a model browser, live SpiralView preview, and retargeting onto a compatible existing UE-style character rig.

This fork also retains **ThreeRingsSharp (TRS / TR#)**, the original C# / IKVM port of Grey Havens' Clyde, Narya, Nenya, and Vilya libraries and its extraction utilities. The Qt studio uses separate Java adapters and Blender scripts; the legacy C# applications remain unchanged.

**Status:** preview software. The Qt export studio is merged into `master`; current limitations and outstanding fixes are listed below.

## Download and run

Download the portable ZIP from [this fork's Releases](https://github.com/TorusXO/ThreeRingsSharp/releases). The latest published package at this update is [ThreeRingsStudio-0.3.14-preview](https://github.com/TorusXO/ThreeRingsSharp/releases/tag/New-0.3.14).

1. Extract the entire ZIP and keep its folders together.
2. Run `ThreeRingsStudio.exe`.
3. Select your Spiral Knights installation and an output folder. Select a saved-scenes folder when exporting archived levels.
4. Choose a tab, select the source, and export. For models, use SpiralView for a read-only live preview or open the exported file in Blender.

The portable package includes Blender 4.2 LTS and a fallback Java 25 runtime. You still need your own game installation and, for levels, saved scene files. Game jars and extracted game assets are not supplied by this repository. Export work prefers a compatible runtime from the game installation.

The source build currently identifies itself as `0.3.2-preview`, while published packages use later release labels. Check `build-identity.json` and `portable-identity.json` in a package for its recorded source and binary identity.

## Features

| Workflow | Current support |
| --- | --- |
| Models | Search game `.dat` paths, inspect hierarchy and parameters, select static-set variants and Choice values, export packed `.blend` files and optional `.glb`. |
| Live preview | Windows canvas-only SpiralView preview, automatic framing, and switching models without restarting the preview JVM. Blender is available for interactive inspection. |
| Knight animations | Export supported stored clips as named Blender Actions on the source rig. The tested installation supplies 150 supported clips. Optional source-bone renaming is off by default. |
| UE-style retargeting | Bake source Actions onto a compatible character supplied as a rigged `.blend`; optional skeletal mesh and per-clip animation FBXs. The tested target is DoubleBelt with 88 bones. |
| Archived levels | Export static geometry, transforms, materials, textures, editor markers, and runtime metadata. A 205-name catalog helps identify archived scenes. |
| Skyboxes | Export geometry and unlit materials, retaining camera-parallax settings as metadata rather than applying them as mesh scale. |
| Shared Blender data | Reuse matching mesh datablocks as linked instances, assign differing materials per object, and deduplicate textures by pixel content and color-space interpretation. |

Exports use source-specific filenames in one reusable output folder. Re-exporting replaces that source's named outputs. Packed Blender files are accompanied by JSON data, reports, and an export log. Review reported limitations even when an export succeeds.

## Animation and UE workflow

Use the **Knight animations** tab to export the source rig and Actions. To retarget, select a compatible rigged character `.blend` before exporting. The pipeline writes a separate `.ue.blend` and retarget report; it does not overwrite the supplied character file.

Retargeting accepts original SK or anatomical source names and preserves the destination bone names, hierarchy, and rest matrices. Extra spine, neck, twist, finger, toe, and IK bones use derived motion where the source rig has no corresponding independent joint. Legacy belt weights may be reassigned to the pelvis; see the outstanding remapping issue below.

Authoring requires **Blender 4.2 LTS**. The scripts do not implement newer layered Action authoring APIs. UE skeletal mesh and animation import still require verification in the destination project, including scale, orientation, timing, deformation, and root-motion behavior.

See the [animation documentation](tools/knight-animations/README.md) for standalone export, retargeting, FBX, and validation commands.

## Build from source

The supported packaging workflow targets **Windows x64**. Install Git, PowerShell, Qt 6 with Widgets and a MinGW kit, CMake, Ninja, and JDK 21. Portable packaging also needs Python, Blender 4.2 LTS, a Java 25 runtime, and the matching KnightLauncher SpiralView jar.

From the repository root:

```powershell
./qt/build.ps1 -Destination ./releases/ThreeRingsStudio-preview

./qt/package-portable.ps1 `
  -ClientDirectory ./releases/ThreeRingsStudio-preview `
  -BlenderDirectory 'C:/path/to/blender-4.2.9-windows-x64' `
  -JavaRuntimeDirectory 'C:/path/to/jdk-25-jre' `
  -SpiralViewJar 'C:/path/to/KnightLauncher/spiralview.jar' `
  -CreateZip
```

Build defaults reference a local Qt 6.11.2 MinGW installation and JDK 21. Override `-QtDirectory`, `-QtToolsDirectory`, and `-JdkDirectory` as needed; the packaging script also accepts `-JdkDirectory`. An optional `-ColorNodeBlend` supplies the editable `ColorReplacementNode` library.

Building the Qt client does not build the legacy C# applications. See the [Qt guide](qt/README.md) for runtime compatibility, deployment, batch options, logs, and test commands.

## Validation and known limitations

The [progress record](docs/export-modernization-progress.md) documents local Windows, Qt, Java, and Blender tests. The recorded DoubleBelt run retargeted 150 Actions and checked 39,600 target bone poses: 88 bones at the first, middle, and last sampled frames of each Action. It reported unchanged rest bones, zero unmapped weighted groups, maximum position error about `0.00000176`, and zero angular error.

These are sampled bake-consistency checks. The pose validator shares the retargeter's pose calculation, so the results do not independently establish motion quality or successful UE import. At the PR #1 merge, the reviewed head had no GitHub Actions runs, check runs, or commit statuses.

Outstanding fixes identified during the merge review:

- **Export publication:** files are replaced individually. A failure midway through publication can leave a mixture of previous and new output files; the full export is not transactional. Keep backups of important existing exports.
- **Belt weights:** the remapper currently transfers certain legacy belt groups to the pelvis even if matching target bones exist. Characters with those bones need a guard before remapping.
- **Validation coverage:** the retarget validator accepts a subset of source clips. It needs an explicit expected-clip check to reject incomplete exports.

Other current limits:

- General model export preserves visible static geometry and textures, but not articulated rigs, skinning, model animation tracks, or complete effect systems. Knight animation export is a separate pipeline.
- Sequential and procedural animation wrappers, runtime layering, transitions, and gameplay events are not fully reconstructed.
- Original-source FBX export has a known weapon-helper discrepancy. Three static clips (`dead`, `hands_closed`, `hold`) may be rejected downstream as empty animation stacks.
- Particle systems, procedural effects, animated texture behavior, fog, lighting, projected shadows, and camera-follow behavior are not a complete recreation of the game renderer. Some archived assets use explicitly reported geometry substitutions.
- The live preview is Windows-only and read-only. Its DWM composite still needs visual confirmation on the user's display.
- Game adapters depend on the tested game jars and reflected entrypoints. Game updates can require compatibility changes.

## Documentation and layout

| Path | Contents |
| --- | --- |
| [qt/](qt/README.md) | Qt studio, Windows build and packaging scripts, preview integration, and runtime setup. |
| [tools/knight-animations/](tools/knight-animations/README.md) | Java export adapters, Blender import and retarget scripts, and validation utilities. |
| [docs/export-modernization-progress.md](docs/export-modernization-progress.md) | Validation history, implementation evidence, and compatibility notes. |
| `ThreeRingsSharp/`, `SKAnimatorTools/`, and `ThreeRingsSharp.sln` | Original C# library and legacy applications. |

## Original project and credits

The original [ThreeRingsSharp project](https://github.com/XanTheDragon/ThreeRingsSharp) ports Clyde-related libraries into C# through IKVM and provides utilities for converting game model data into common formats, including glTF 2.0. Its library and legacy utilities remain part of this fork.

Historical screenshots below show the **legacy interface**, not the current Qt studio.

<details>
<summary>Legacy interface screenshots</summary>

![Legacy interface](https://i.imgur.com/M75YpwE.png)

![Legacy customization](https://i.imgur.com/5uRgxBE.png)

![Legacy implementation support](https://i.imgur.com/rdhWgIL.png)

</details>

## Licensing

See [LICENSE](LICENSE) for project code and [_LICENSES_EXTERNAL.MD](_LICENSES_EXTERNAL.MD) for external-library notices. Converted IKVM DLLs and other third-party components retain their own licenses; the project's MIT license does not relicense them.

Portable distributions must retain the license notices for their bundled dependencies, including Qt, Blender, Java, SpiralView, and Clyde. Do not commit game jars, extracted game assets, runtime copies, or generated exports to Git.
