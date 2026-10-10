# Knight animation extraction

This extension adds a command-line export path for player-knight animation data.
It uses the locally installed game's own BinaryImporter through a small Java
adapter, then constructs the source armature and Actions in Blender 4.2 LTS.
It does not change the old Windows GUI or its level exporter.

## Why the old versions missed the animations

* The IKVM reader in `master` documents the unknown `ProjectXModelConfig` problem.
  The current flag bypasses its early rejection, but bypassing that check does not
  supply the game's custom classes. A player model actually contains a
  `ProjectXModelConfig$WalkingConfig` implementation.
* Opening an AnimationConfig directly is explicitly rejected by ClydeFileHandler.
* In `OOOReaderImpl` (3bd8d9b), ArticulatedConfig reads animation names for the UI
  but never decodes their tracks; MasterDataExtractor's AnimationConfig branch is
  commented out. That branch has scene-reading work, explaining why level support
  and animation support differ. This is not a diagnosis of every level issue.
* The supplied net6 OOOReader binary also failed on the installed
  `character/pc/run_forward.dat` with an array-type exception. Reading that same
  file through the installed game succeeds.
* The old animation handler ignores Imported.speed, removes another last frame
  even though skipLastFrame is applied during source import, and its null-frame
  interpolation retains a null local variable and chooses the last future key.
  The new path bypasses that handler rather than claiming those GUI bugs are fixed.

## Export

Requirements: a local Spiral Knights installation, a JDK 11+ for compilation,
and a Java runtime compatible with that installation's jars. Compilation uses
reflection and does not link or redistribute game classes. The supplied launcher
uses the game's Java runtime by default. For installations upgraded to Java 25
that retain obsolete `lib/ext` files, it makes a private runtime copy excluding
that directory; the original installation is unchanged.

```powershell
./tools/knight-animations/export.ps1 `
  -GameDirectory 'E:/SteamLibrary/steamapps/common/Spiral Knights' `
  -JdkDirectory 'C:/Program Files/Android/openjdk/jdk-21.0.8' `
  -OutputDirectory 'E:/exports/knight-new'
```

Choose a new output directory. The bundle contains the full parent hierarchy,
local rest matrices, named clips, source frame rates multiplied by configured
speed, and local animated matrices. Matrices retain mirrored transforms and
scales. Game-defined target modifiers and reversal are applied. Unknown targets
are listed per clip and ignored, matching the game's runtime behavior.

```powershell
blender --background --python-exit-code 1 `
  --python tools/knight-animations/import_blender.py -- `
  E:/exports/knight-new/knight-animations.json `
  E:/exports/knight-new/Spiral-Knight-Animations.blend `
  E:/exports/knight-new/fbx
```

Use **Blender 4.2 LTS to run the importer**. Later Blender versions can open the
saved .blend; this script does not yet implement their layered Action authoring
API. The optional third path exports one FBX per Action at 60 fps. Existing output
files are not overwritten. The importer preserves existing scene objects.

Open the .blend, select **Spiral Knight**, switch the Dope Sheet to Action Editor,
and choose an Action such as `idle`, `run_forward`, or `attack_sword_1_fire`.
The file contains the source rig, not a character mesh. Bind your existing model
to this rig or retarget these Actions to your destination armature. Renaming bones
alone does not convert the rig to the UE5 mannequin: rest poses, bone axes,
proportions, root motion, and hand/foot chains need retargeting.

## Coverage and limitations

Tested against the user's installed resources on 2026-10-10:

* **150 imported clips, 49 skeleton nodes.** Names and all stored frames are kept.
  Looping clips receive an explicit closing key at the full loop period.
* Eight files are reported as unsupported: six Sequential wrappers
  (`attack_tortagun/fire`, looping clap/salute, stormcall, trick, shieldbash) and two
  Procedural files (`dance`, `animation_heightadjust`). Underlying imported parts
  are exported where present, but these wrappers are not reconstructed.
* These are independent default-config clips. Runtime layering, random offsets,
  transitions from the current pose, parameter variants, sound/particle events,
  equipment attachments, and procedural gameplay behavior are not baked.
* Source matrices are already scaled by the game authoring pipeline. Do not apply
  Imported.scale (often 0.01) a second time.
* FBX is baked at 60 fps and can differ between sampled frames. A known Blender
  FBX round-trip issue affects the `bone_weapon_r` attachment in several clips,
  particularly `use_inventory_start`. **Use the .blend as the authoritative
  retargeting source**; its attachment matrices pass the source-pose validation.
  All other bones passed the tested FBX position comparisons. UE5 import and
  mannequin retargeting have not been validated by this change.

## Validation

```powershell
blender --background E:/exports/knight-new/Spiral-Knight-Animations.blend `
  --python-exit-code 1 --python tools/knight-animations/validate_blender.py -- `
  E:/exports/knight-new/knight-animations.json

blender --background E:/exports/knight-new/Spiral-Knight-Animations.blend `
  --python-exit-code 1 --python tools/knight-animations/validate_fbx.py -- `
  E:/exports/knight-new/fbx
```

The first check evaluates first/middle/last source poses for every Action and
compares all 4x4 bone matrices. The tested bundle passed **21,609 bone-pose checks**
with a maximum error of **0.00000221**. The second compares all exported FBX files
at three integer baked frames and writes `fbx-validation.json`, including known
weapon-attachment differences rather than silently accepting them. These are
sampled checks, not an exhaustive validation of every interpolated pose.

No game jars, extracted assets, runtime copies, or Blender binaries belong in Git.
The local output and build directories are ignored. Existing upstream licenses
are preserved; this helper is covered by the repository's MIT license.

Source references:
[TRS master](https://github.com/EtiTheSpirit/ThreeRingsSharp),
[newer reader branch](https://github.com/EtiTheSpirit/ThreeRingsSharp/tree/OOOReaderImpl),
[Clyde animation configuration](https://github.com/threerings/clyde/blob/master/core/src/main/java/com/threerings/opengl/model/config/AnimationConfig.java),
[Clyde animation runtime](https://github.com/threerings/clyde/blob/master/core/src/main/java/com/threerings/opengl/model/Animation.java).
