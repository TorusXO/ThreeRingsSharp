param(
    [Parameter(Mandatory=$true)][string]$ClientDirectory,
    [Parameter(Mandatory=$true)][string]$BlenderDirectory,
    [Parameter(Mandatory=$true)][string]$JavaRuntimeDirectory,
    [Parameter(Mandatory=$true)][string]$SpiralViewJar,
    [string]$JdkDirectory = 'C:/Program Files/Android/openjdk/jdk-21.0.8',
    [string]$ClydeJar,
    [string]$ColorNodeBlend,
    [switch]$CreateZip
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$client = [IO.Path]::GetFullPath($ClientDirectory)
$blender = [IO.Path]::GetFullPath($BlenderDirectory)
$runtime = [IO.Path]::GetFullPath($JavaRuntimeDirectory)
$viewer = [IO.Path]::GetFullPath($SpiralViewJar)
foreach ($file in @("$client/ThreeRingsStudio.exe", "$blender/blender.exe", "$runtime/bin/java.exe", $viewer)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Portable package input is missing: $file" }
}
$javac = Join-Path ([IO.Path]::GetFullPath($JdkDirectory)) 'bin/javac.exe'
if (-not (Test-Path -LiteralPath $javac -PathType Leaf)) { throw "JDK compiler is missing: $javac" }
function Copy-Tree([string]$Source, [string]$Target) {
    New-Item -ItemType Directory -Force -Path $Target | Out-Null
    & "$env:SystemRoot/System32/robocopy.exe" $Source $Target /E /XF '*.pdb' /R:1 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -gt 7) { throw "Could not copy $Source to $Target (robocopy exit $LASTEXITCODE)" }
}
Copy-Tree $blender "$client/tools/blender"
Copy-Tree $runtime "$client/runtime"
$buildDeps = Join-Path $repo '.local/package-dependencies'
New-Item -ItemType Directory -Force $buildDeps | Out-Null
if (-not $ClydeJar) {
    $ClydeJar = Join-Path $buildDeps 'clyde-1.2.1.jar'
    if (-not (Test-Path -LiteralPath $ClydeJar -PathType Leaf)) {
        Invoke-WebRequest -Uri 'https://repo1.maven.org/maven2/com/threerings/clyde/1.2.1/clyde-1.2.1.jar' -OutFile $ClydeJar
    }
}
$ClydeJar = [IO.Path]::GetFullPath($ClydeJar)
$expectedClydeSha256 = '02036ABF69BB8462FCAD27ECD0397719B8EF09B0E5405207F732C301E9C05875'
if ((Get-FileHash $ClydeJar -Algorithm SHA256).Hash -ne $expectedClydeSha256) {
    throw "Clyde 1.2.1 archive has an unexpected SHA256: $ClydeJar"
}
$compat = Join-Path $buildDeps 'spiralview-compat'
New-Item -ItemType Directory -Force "$compat/classes" | Out-Null
python "$PSScriptRoot/patch_spiralview.py" prepare-classpath $viewer "$compat/compiler-classpath.jar"
if ($LASTEXITCODE) { throw 'Could not prepare the SpiralView compatibility classpath' }
$compatSources = Get-ChildItem -LiteralPath "$PSScriptRoot/spiralview-compat" -Filter '*.java' -Recurse
& $javac --release 21 -cp "$compat/compiler-classpath.jar" -d "$compat/classes" $compatSources.FullName
if ($LASTEXITCODE) { throw 'Could not compile the SpiralView compatibility class' }
python "$PSScriptRoot/patch_spiralview.py" build $viewer $ClydeJar "$compat/classes" "$client/backend/spiralview.jar"
if ($LASTEXITCODE) { throw 'Could not package the SpiralView compatibility layer' }
Copy-Item -LiteralPath "$PSScriptRoot/third-party/spiralview-LICENSE.txt" -Destination "$client/backend/spiralview-LICENSE.txt" -Force
Copy-Item -LiteralPath "$PSScriptRoot/third-party/clyde-LICENSE.txt" -Destination "$client/backend/clyde-LICENSE.txt" -Force
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
