$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
function Resolve-GameDirectory([string]$Directory) {
    if ($Directory) {
        $path = (Resolve-Path -LiteralPath $Directory).Path
        if (-not (Test-Path -LiteralPath (Join-Path $path 'Ace7Game.exe'))) { throw "Ace7Game.exe not found: $path" }
        return $path
    }
    $steam = (Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
    $libraries = @($steam)
    if ($steam) {
        $vdf = Join-Path $steam 'steamapps\libraryfolders.vdf'
        if (Test-Path -LiteralPath $vdf) {
            foreach ($match in [regex]::Matches([IO.File]::ReadAllText($vdf), '"path"\s+"([^"]+)"')) {
                $libraries += $match.Groups[1].Value.Replace('\\', '\')
            }
        }
    }
    $games = @($libraries | Where-Object { $_ } | ForEach-Object {
        Join-Path $_ 'steamapps\common\ACE COMBAT 7'
    } | Where-Object { Test-Path -LiteralPath (Join-Path $_ 'Ace7Game.exe') } | Select-Object -Unique)
    if ($games.Count -ne 1) { throw 'Specify -GameDirectory with the folder containing Ace7Game.exe.' }
    return $games[0]
}
function Assert-Closed {
    if (Get-Process Ace7Game -ErrorAction SilentlyContinue) { throw 'Exit ACE COMBAT 7 before installing or uninstalling.' }
}
function Write-Utf8([string]$Path, [string]$Text) {
    [IO.File]::WriteAllText($Path, $Text, (New-Object Text.UTF8Encoding($false)))
}
function Get-ModLists([string]$Mods, [bool]$Enabled) {
    $txt = Join-Path $Mods 'mods.txt'
    $text = if (Test-Path -LiteralPath $txt) { [IO.File]::ReadAllText($txt) } else { '' }
    $pattern = '(?m)^[ \t]*AC7Telemetry[ \t]*:[ \t]*\d+[^\r\n]*(?:\r?\n|$)'
    $text = [regex]::Replace($text, $pattern, '')
    if ($Enabled) { $text = $text.TrimEnd() + "`r`nAC7Telemetry : 1`r`n" }
    $json = Join-Path $Mods 'mods.json'
    $entries = if (Test-Path -LiteralPath $json) { @(ConvertFrom-Json -InputObject ([IO.File]::ReadAllText($json)) | ForEach-Object { $_ }) } else { @() }
    $entries = @($entries | Where-Object { $_.mod_name -ne 'AC7Telemetry' })
    if ($Enabled) { $entries += [pscustomobject]@{ mod_name = 'AC7Telemetry'; mod_enabled = $true } }
    return @{ Text = $text; Json = (ConvertTo-Json -InputObject @($entries) -Depth 20) }
}
