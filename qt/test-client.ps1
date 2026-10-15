param(
    [Parameter(Mandatory=$true)][string]$Client,
    [Parameter(Mandatory=$true)][string]$GameDirectory,
    [Parameter(Mandatory=$true)][string]$Blender,
    [Parameter(Mandatory=$true)][string]$Java,
    [Parameter(Mandatory=$true)][string]$TestDirectory
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Path $TestDirectory -Force | Out-Null
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
$invalid = Join-Path $TestDirectory 'invalid-scene'
'not a scene' | Set-Content $invalid
Check-Failure 'invalid scene stops pipeline in existing folder' ($common + @('--batch','scene','--source',$invalid,'--output',$existing))
Check-Failure 'failed process launch in existing folder' @('--game',$GameDirectory,'--blender',$Blender,'--java',(Join-Path (Split-Path $Client) 'backend/trs-export.jar'),'--batch','animation','--output',$existing)
Check-Failure 'cancel stops pipeline in existing folder' ($common + @('--batch','animation','--output',$existing,'--cancel-after-ms','20'))
if ((Get-FileHash (Join-Path $existing 'sentinel.txt')).Hash -ne $before) { throw 'Unrelated output changed' }
if (Get-ChildItem -LiteralPath $existing -Filter '*.blend') { throw 'Failure path incorrectly produced a blend' }
if (Get-ChildItem -LiteralPath $existing -Force -Directory -Filter '.trs-stage-*') { throw 'Temporary staging folder was retained' }
$results | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $TestDirectory 'client-test-results.json')
$results | Format-Table
