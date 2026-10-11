param(
    [Parameter(Mandatory=$true)][string]$Client,
    [Parameter(Mandatory=$true)][string]$GameDirectory,
    [Parameter(Mandatory=$true)][string]$Blender,
    [Parameter(Mandatory=$true)][string]$Java,
    [Parameter(Mandatory=$true)][string]$TestDirectory
)
$ErrorActionPreference = 'Stop'
if (Test-Path -LiteralPath $TestDirectory) { throw 'Use a new test directory' }
New-Item -ItemType Directory -Path $TestDirectory | Out-Null
$common = @('--game', $GameDirectory, '--blender', $Blender, '--java', $Java)
$results = [System.Collections.Generic.List[object]]::new()
function Check-Failure([string]$Name, [string[]]$Arguments) {
    & $Client @Arguments
    if ($LASTEXITCODE -ne 1) { throw "$Name expected exit 1, got $LASTEXITCODE" }
    $results.Add(@{test=$Name; exitCode=$LASTEXITCODE; passed=$true})
}
$existing = Join-Path $TestDirectory 'existing'
New-Item -ItemType Directory -Path $existing | Out-Null
'preserve me' | Set-Content (Join-Path $existing 'sentinel.txt')
$before = (Get-FileHash (Join-Path $existing 'sentinel.txt')).Hash
Check-Failure 'existing output protection' ($common + @('--batch','animation','--output',$existing))
if ((Get-FileHash (Join-Path $existing 'sentinel.txt')).Hash -ne $before) { throw 'Existing output changed' }
$invalid = Join-Path $TestDirectory 'invalid-scene'
'not a scene' | Set-Content $invalid
Check-Failure 'invalid scene stops pipeline' ($common + @('--batch','scene','--source',$invalid,'--output',(Join-Path $TestDirectory 'invalid-output')))
Check-Failure 'failed process launch' @('--game',$GameDirectory,'--blender',$Blender,'--java',(Join-Path (Split-Path $Client) 'backend/trs-export.jar'),'--batch','animation','--output',(Join-Path $TestDirectory 'failed-launch'))
Check-Failure 'cancel stops pipeline' ($common + @('--batch','animation','--output',(Join-Path $TestDirectory 'cancelled'),'--cancel-after-ms','20'))
if (Get-ChildItem -LiteralPath $TestDirectory -Recurse -Filter '*.blend') { throw 'Failure path incorrectly produced a blend' }
$results | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $TestDirectory 'client-test-results.json')
$results | Format-Table
