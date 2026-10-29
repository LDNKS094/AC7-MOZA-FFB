[CmdletBinding(SupportsShouldProcess = $true)]
param([string]$GameDirectory)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Common.ps1')
$game = Resolve-GameDirectory $GameDirectory
Assert-Closed
foreach ($dependency in @('dinput8.dll', 'ue4ss\UE4SS.dll', 'ue4ss\Mods\shared\UEHelpers\UEHelpers.lua')) {
    if (-not (Test-Path -LiteralPath (Join-Path $game $dependency))) { throw "Missing dependency: $dependency. See README.md." }
}
$mods = Join-Path $game 'ue4ss\Mods'
$lists = Get-ModLists $mods $true
$files = [ordered]@{
    'AC7RumbleTrace.asi' = (Join-Path $ProjectRoot 'payload\AC7RumbleTrace.asi')
    'AC7ExpandedInput.asi' = (Join-Path $ProjectRoot 'payload\AC7ExpandedInput.asi')
    'ue4ss\Mods\AC7Telemetry\Scripts\main.lua' = (Join-Path $ProjectRoot 'mod\AC7Telemetry\Scripts\main.lua')
}
$inputSource = Join-Path $ProjectRoot 'presets\input\Input.ini'
$rumbleSource = Join-Path $ProjectRoot 'presets\RumbleSettings.ini'
$rumblePath = Join-Path $game 'RumbleSettings.ini'
$inputPath = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'BANDAI NAMCO Entertainment\ACE COMBAT 7\Config\Input.ini'
if (-not (Test-Path -LiteralPath $inputPath -PathType Leaf)) {
    throw 'Input.ini not found. Connect your controllers, start AC7 once, then exit before installing.'
}
function Merge-InputBindings([string]$Text, [string]$Template) {
    $sectionPattern = '(?ms)^\[(?<section>[^\]\r\n]+)\][^\r\n]*(?:\r?\n|$).*?(?=^\[|\z)'
    $productPattern = '(?m)^[ \t]*ProductName[ \t]*=[ \t]*(?<name>[^\r\n]+)'
    $defaults = @{}
    foreach ($block in [regex]::Matches($Template, $sectionPattern)) {
        $name = [regex]::Match($block.Value, $productPattern).Groups['name'].Value.Trim()
        $values = [ordered]@{}
        foreach ($entry in [regex]::Matches($block.Value, '(?m)^(?<key>(?:Flight_|UI_)\w+)[ \t]*=(?<value>[^\r\n]*)')) {
            $values[$entry.Groups['key'].Value] = $entry.Groups['value'].Value
        }
        if ($name) { $defaults[$name] = $values }
    }
    $blocks = [regex]::Matches($Text, $sectionPattern)
    $devices = @($blocks | Where-Object {
        $_.Groups['section'].Value -like 'Joystick-*' -and
        $defaults.ContainsKey([regex]::Match($_.Value, $productPattern).Groups['name'].Value.Trim())
    })
    if ($devices.Count -eq 0) { return [pscustomobject]@{Text = $Text; Devices = @()} }
    $newline = if ($Text.Contains("`r`n")) { "`r`n" } else { "`n" }
    for ($i = $blocks.Count - 1; $i -ge 0; $i--) {
        $block = $blocks[$i]
        if ($block.Groups['section'].Value -notlike 'Joystick-*') { continue }
        $name = [regex]::Match($block.Value, $productPattern).Groups['name'].Value.Trim()
        if (-not $defaults.ContainsKey($name)) { continue }
        $body = $block.Value
        foreach ($key in $defaults[$name].Keys) {
            $line = $key + '=' + $defaults[$name][$key]
            $pattern = '(?m)^[ \t]*' + [regex]::Escape($key) + '[ \t]*=[^\r\n]*'
            if ([regex]::IsMatch($body, $pattern)) {
                $body = [regex]::Replace($body, $pattern, [System.Text.RegularExpressions.MatchEvaluator]{ param($match) $line })
            } else {
                if (-not $body.EndsWith("`n")) { $body += $newline }
                $body += $line + $newline
            }
        }
        $Text = $Text.Remove($block.Index, $block.Length).Insert($block.Index, $body)
    }
    return [pscustomobject]@{Text = $Text; Devices = @($devices | ForEach-Object { $_.Groups['section'].Value })}
}
foreach ($source in @($files.Values) + @($inputSource, $rumbleSource)) {
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing package file: $source" }
}
$inputBindings = Merge-InputBindings ([IO.File]::ReadAllText($inputPath)) ([IO.File]::ReadAllText($inputSource))
$library = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'MOZA Cockpit\PresetLibrary'
if (-not (Test-Path -LiteralPath $library -PathType Container)) { throw "PresetLibrary not found: $library" }
$source = Join-Path $ProjectRoot 'presets\{71ab7001-0014-4a70-8001-000000000004}.preset'
$preset = Get-Content -LiteralPath $source -Raw | ConvertFrom-Json
$indexPath = Join-Path $library 'PresetIndex.index'
$matchPath = Join-Path $library 'OptimalMatch.vconfig'
$index = if (Test-Path -LiteralPath $indexPath) { Get-Content -LiteralPath $indexPath -Raw | ConvertFrom-Json } else { [pscustomobject]@{presets = @()} }
$match = if (Test-Path -LiteralPath $matchPath) { Get-Content -LiteralPath $matchPath -Raw | ConvertFrom-Json } else { [pscustomobject]@{Base = [pscustomobject]@{}; version = '1.0.4'} }
foreach ($device in $preset.devices) {
    foreach ($presetGame in $preset.games) {
        $parent = $match
        foreach ($level in @($preset.device_type, $device, $presetGame)) {
            if (-not $parent.PSObject.Properties[$level]) { $parent | Add-Member $level ([pscustomobject]@{}) }
            $parent = $parent.$level
        }
        $parent | Add-Member $preset.vehicles $preset.uuid -Force
    }
}
$destination = Join-Path $library "$($preset.uuid).preset"
$entry = [pscustomobject]@{date = $preset.date; device_type = $preset.device_type; devices = @($preset.devices)
    file_path = $destination.Replace('\', '/'); games = @($preset.games); name = $preset.name
    product_type = $preset.product_type; uuid = $preset.uuid; vehicles = $preset.vehicles; version = $preset.version}
$index.presets = @($index.presets | Where-Object { $_.uuid -ne $preset.uuid }) + @($entry)
$indexJson = $index | ConvertTo-Json -Depth 30
$matchJson = $match | ConvertTo-Json -Depth 30
if (-not $PSCmdlet.ShouldProcess($game, 'Overwrite AC7-MOZA-FFB game components and AC7 Default preset; install missing rumble settings; back up and update known input bindings')) { return }
if ($inputBindings.Devices.Count -gt 0) {
    $inputBackup = $inputPath + '.' + (Get-Date -Format 'yyyyMMdd-HHmmss-fffffff') + '.bak'
    [IO.File]::Copy($inputPath, $inputBackup)
    Write-Output "Input.ini backup: $inputBackup"
    Write-Utf8 $inputPath $inputBindings.Text
}
foreach ($relative in $files.Keys) {
    $target = Join-Path $game $relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
    Copy-Item -LiteralPath $files[$relative] -Destination $target -Force
}
if (-not (Test-Path -LiteralPath $rumblePath)) {
    Copy-Item -LiteralPath $rumbleSource -Destination $rumblePath
}
Write-Utf8 (Join-Path $mods 'mods.txt') $lists.Text
Write-Utf8 (Join-Path $mods 'mods.json') $lists.Json
Copy-Item -LiteralPath $source -Destination $destination -Force
Write-Utf8 $indexPath $indexJson
Write-Utf8 $matchPath $matchJson
Write-Output "Installed to: $game"
Write-Output 'ASI plugins, telemetry script and AC7 Default were overwritten without backups. Existing RumbleSettings.ini, other mods and other presets were preserved.'
if ($inputBindings.Devices.Count -gt 0) {
    Write-Output "Updated input bindings in: $inputPath"
    Write-Output ('Matched device sections: ' + ($inputBindings.Devices -join ', '))
} else {
    Write-Output 'No device with a supplied input binding scheme found; Input.ini was preserved.'
}
Write-Output 'Existing device GUIDs, other device sections and unrelated input settings were preserved.'
Write-Output 'Open Cockpit and ACE COMBAT 7, then double-click DCS.exe.'
