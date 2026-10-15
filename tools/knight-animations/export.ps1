param(
    [Parameter(Mandatory=$true)][string]$GameDirectory,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][string]$JdkDirectory,
    [string]$JavaExecutable
)
$ErrorActionPreference = 'Stop'
$game = (Resolve-Path -LiteralPath $GameDirectory).Path
$output = [IO.Path]::GetFullPath($OutputDirectory)
$compiler = Join-Path $JdkDirectory 'bin/javac.exe'
if (-not (Test-Path -LiteralPath $compiler)) { throw "JDK compiler missing: $compiler" }
if (Test-Path -LiteralPath $output) { throw "Use a new output directory: $output" }
if (-not (Test-Path -LiteralPath (Join-Path $game 'rsrc/character/pc/model.dat'))) { throw 'Knight model not found in this game installation.' }
$build = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null
& $compiler --release 11 -d $build (Join-Path $PSScriptRoot 'KnightAnimationExport.java')
if ($LASTEXITCODE -ne 0) { throw 'Exporter compilation failed.' }
if (-not $JavaExecutable) {
    # Some upgraded launchers leave Java 8 lib/ext files in a Java 25 runtime.
    # Copy the runtime locally, excluding obsolete ext files; never alter the game.
    $installed = Join-Path $game 'java_vm'
    if (Test-Path -LiteralPath (Join-Path $installed 'lib/ext')) {
        $runtime = Join-Path $build 'game-java'
        & robocopy $installed $runtime /E /XD ext /NFL /NDL /NJH /NJS /NP | Out-Null
        if ($LASTEXITCODE -ge 8) { throw 'Could not prepare a local game Java runtime.' }
        $JavaExecutable = Join-Path $runtime 'bin/java.exe'
    } else { $JavaExecutable = Join-Path $installed 'bin/java.exe' }
}
if (-not (Test-Path -LiteralPath $JavaExecutable)) { throw 'Supply -JavaExecutable with a Java runtime compatible with the game jars.' }
$classpath = $build + [IO.Path]::PathSeparator + (Join-Path $game 'code/*')
& $JavaExecutable -Xshare:off -cp $classpath KnightAnimationExport (Join-Path $game 'rsrc') $output
if ($LASTEXITCODE -ne 0) { throw 'Animation export failed.' }
