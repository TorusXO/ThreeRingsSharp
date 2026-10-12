param(
    [Parameter(Mandatory=$true)][string]$ClientDirectory,
    [Parameter(Mandatory=$true)][string]$BlenderDirectory,
    [Parameter(Mandatory=$true)][string]$JavaRuntimeDirectory,
    [Parameter(Mandatory=$true)][string]$SpiralViewJar,
    [Parameter(Mandatory=$true)][string]$Lwjgl2Directory,
    [string]$ColorNodeBlend,
    [switch]$CreateZip
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$client = [IO.Path]::GetFullPath($ClientDirectory)
$blender = [IO.Path]::GetFullPath($BlenderDirectory)
$runtime = [IO.Path]::GetFullPath($JavaRuntimeDirectory)
$viewer = [IO.Path]::GetFullPath($SpiralViewJar)
$lwjgl = [IO.Path]::GetFullPath($Lwjgl2Directory)
foreach ($file in @("$client/ThreeRingsStudio.exe", "$blender/blender.exe", "$runtime/bin/java.exe", $viewer)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Portable package input is missing: $file" }
}
foreach ($file in @("$lwjgl/lwjgl-2.9.3.jar", "$lwjgl/lwjgl_util-2.9.3.jar", "$lwjgl/native/lwjgl64.dll")) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "SpiralView dependency is missing: $file" }
}
function Copy-Tree([string]$Source, [string]$Target) {
    New-Item -ItemType Directory -Force -Path $Target | Out-Null
    & "$env:SystemRoot/System32/robocopy.exe" $Source $Target /E /XF '*.pdb' /R:1 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -gt 7) { throw "Could not copy $Source to $Target (robocopy exit $LASTEXITCODE)" }
}
Copy-Tree $blender "$client/tools/blender"
Copy-Tree $runtime "$client/runtime"
Copy-Item -LiteralPath $viewer -Destination "$client/backend/spiralview.jar" -Force
Copy-Item -LiteralPath "$PSScriptRoot/third-party/spiralview-LICENSE.txt" -Destination "$client/backend/spiralview-LICENSE.txt" -Force
New-Item -ItemType Directory -Force "$client/backend/lwjgl2", "$client/backend/preview-native" | Out-Null
Copy-Item -LiteralPath "$lwjgl/lwjgl-2.9.3.jar", "$lwjgl/lwjgl_util-2.9.3.jar" -Destination "$client/backend/lwjgl2" -Force
Get-ChildItem -LiteralPath "$lwjgl/native" -Filter '*.dll' | Copy-Item -Destination "$client/backend/preview-native" -Force
Copy-Item -LiteralPath "$PSScriptRoot/third-party/lwjgl2-LICENSE.txt" -Destination "$client/backend/lwjgl2-LICENSE.txt" -Force
if ($ColorNodeBlend) {
    Copy-Item -LiteralPath ([IO.Path]::GetFullPath($ColorNodeBlend)) -Destination "$client/backend/Color_Replacement_Node.blend" -Force
}
$identity = [ordered]@{
    client = (Get-FileHash "$client/ThreeRingsStudio.exe" -Algorithm SHA256).Hash
    blenderVersion = (& "$client/tools/blender/blender.exe" --background --factory-startup --version | Select-Object -First 1)
    blender = (Get-FileHash "$client/tools/blender/blender.exe" -Algorithm SHA256).Hash
    javaVersion = ((& "$client/runtime/bin/java.exe" -version 2>&1 | Select-Object -First 1) -join '')
    java = (Get-FileHash "$client/runtime/bin/java.exe" -Algorithm SHA256).Hash
    spiralView = (Get-FileHash "$client/backend/spiralview.jar" -Algorithm SHA256).Hash
    packagedUtc = [DateTime]::UtcNow.ToString('o')
}
$identity | ConvertTo-Json | Set-Content -Encoding utf8 "$client/portable-identity.json"
if ($CreateZip) {
    $zip = "$client.zip"
    if (Test-Path -LiteralPath $zip) { throw "Refusing to replace existing portable archive: $zip" }
    & tar.exe -a -c -f $zip -C (Split-Path $client -Parent) (Split-Path $client -Leaf)
    if ($LASTEXITCODE) { throw "Could not create portable archive: $zip" }
    Write-Output "Portable ZIP: $zip"
    Write-Output "SHA256: $((Get-FileHash $zip -Algorithm SHA256).Hash)"
}
Write-Output "Portable client: $client/ThreeRingsStudio.exe"
