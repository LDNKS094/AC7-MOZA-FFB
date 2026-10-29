[CmdletBinding(SupportsShouldProcess = $true)]
param([string]$GameDirectory)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Common.ps1')
$game = Resolve-GameDirectory $GameDirectory
Assert-Closed
$uuid = '{71ab7001-0014-4a70-8001-000000000004}'
$library = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'MOZA Cockpit\PresetLibrary'
$indexPath = Join-Path $library 'PresetIndex.index'
$matchPath = Join-Path $library 'OptimalMatch.vconfig'
$index = if (Test-Path -LiteralPath $indexPath) { Get-Content -LiteralPath $indexPath -Raw | ConvertFrom-Json } else { $null }
$match = if (Test-Path -LiteralPath $matchPath) { Get-Content -LiteralPath $matchPath -Raw | ConvertFrom-Json } else { $null }
if ($index) { $index.presets = @($index.presets | Where-Object { $_.uuid -ne $uuid }) }
$mapping = if ($match -and $match.Base -and $match.Base.AB6) { $match.Base.AB6.dcs_world } else { $null }
if ($mapping -and $mapping.PSObject.Properties['AC7 Default'] -and $mapping.'AC7 Default' -eq $uuid) {
    $mapping.PSObject.Properties.Remove('AC7 Default')
}
$mods = Join-Path $game 'ue4ss\Mods'
$lists = Get-ModLists $mods $false
if (-not $PSCmdlet.ShouldProcess($game, 'Remove AC7-MOZA-FFB game components and Cockpit preset')) { return }
foreach ($name in @('AC7RumbleTrace.asi', 'AC7ExpandedInput.asi', 'RumbleSettings.ini')) {
    $path = Join-Path $game $name
    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path }
}
$telemetry = Join-Path $mods 'AC7Telemetry'
if (Test-Path -LiteralPath $telemetry) { Remove-Item -LiteralPath $telemetry -Recurse }
if (Test-Path -LiteralPath $mods) {
    Write-Utf8 (Join-Path $mods 'mods.txt') $lists.Text
    Write-Utf8 (Join-Path $mods 'mods.json') $lists.Json
}
$presetPath = Join-Path $library "$uuid.preset"
if (Test-Path -LiteralPath $presetPath) { Remove-Item -LiteralPath $presetPath }
if ($index) { Write-Utf8 $indexPath ($index | ConvertTo-Json -Depth 30) }
if ($match) { Write-Utf8 $matchPath ($match | ConvertTo-Json -Depth 30) }
Write-Output 'Removed this project game components and Cockpit preset. Input.ini, its backups, other presets, recorded logs and other mods were preserved.'
