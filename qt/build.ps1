param(
    [string]$QtDirectory = 'C:/qt/6.11.2/mingw_64',
    [string]$QtToolsDirectory = 'C:/qt/Tools',
    [string]$JdkDirectory = 'C:/Program Files/Android/openjdk/jdk-21.0.8',
    [string]$BuildDirectory = "$PSScriptRoot/../.local/qt-build",
    [string]$Destination = "$PSScriptRoot/../.local/ThreeRingsStudio"
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$build = [IO.Path]::GetFullPath($BuildDirectory)
$dest = [IO.Path]::GetFullPath($Destination)
$env:PATH = "$QtToolsDirectory/mingw1310_64/bin;$QtDirectory/bin;" + $env:PATH
$cmake = "$QtToolsDirectory/CMake_64/bin/cmake.exe"
& $cmake -S $PSScriptRoot -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$QtToolsDirectory/Ninja/ninja.exe" "-DCMAKE_PREFIX_PATH=$QtDirectory" -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE) { throw 'CMake configuration failed' }
& $cmake --build $build
if ($LASTEXITCODE) { throw 'C++ build failed' }
New-Item -ItemType Directory -Force "$dest/backend", "$build/java" | Out-Null
& "$JdkDirectory/bin/javac.exe" --release 11 -d "$build/java" "$repo/tools/knight-animations/KnightAnimationExport.java" "$repo/tools/knight-animations/SceneExport.java"
if ($LASTEXITCODE) { throw 'Java helper compilation failed' }
& "$JdkDirectory/bin/jar.exe" --create --file "$dest/backend/trs-export.jar" -C "$build/java" .
if ($LASTEXITCODE) { throw 'Backend packaging failed' }
Copy-Item -LiteralPath "$build/ThreeRingsStudio.exe" -Destination $dest
Get-ChildItem -LiteralPath "$repo/tools/knight-animations" -Filter '*.py' | Copy-Item -Destination "$dest/backend"
# This compatibility config is already part of the original repository.
Copy-Item -LiteralPath "$repo/ThreeRingsSharp/ConfigRefs/scene_global.xml" -Destination "$dest/backend"
Copy-Item -LiteralPath "$PSScriptRoot/scene-names.tsv", "$PSScriptRoot/scene-fingerprints.tsv" -Destination "$dest/backend"
& "$QtDirectory/bin/windeployqt.exe" --release --no-translations --no-opengl-sw --compiler-runtime "$dest/ThreeRingsStudio.exe"
if ($LASTEXITCODE) { throw 'Qt deployment failed' }
Copy-Item -LiteralPath "$repo/LICENSE" -Destination "$dest/LICENSE-ThreeRingsSharp.txt"
Copy-Item -LiteralPath "$PSScriptRoot/README.md" -Destination "$dest/README.md"
if (Test-Path "$QtDirectory/../../Licenses") { Copy-Item "$QtDirectory/../../Licenses" "$dest/Qt-Licenses" -Recurse -Force }
$identity = [ordered]@{
    version = '0.2.5-preview'; profile = 'Release / Qt 6 / MinGW x64'; builtUtc = [DateTime]::UtcNow.ToString('o')
    qtVersion = (& "$QtDirectory/bin/qmake.exe" -query QT_VERSION)
    commit = (& git -C $repo rev-parse HEAD); sourceDirty = [bool](& git -C $repo status --porcelain)
    executableSha256 = (Get-FileHash "$dest/ThreeRingsStudio.exe" -Algorithm SHA256).Hash
    backendSha256 = (Get-FileHash "$dest/backend/trs-export.jar" -Algorithm SHA256).Hash
}
$identity | ConvertTo-Json | Set-Content -Encoding utf8 "$dest/build-identity.json"
Write-Output "Built: $dest/ThreeRingsStudio.exe"
