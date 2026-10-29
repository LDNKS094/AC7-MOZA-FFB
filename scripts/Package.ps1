param([Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]*$')][string]$Version)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$stage = Join-Path $root ('development\artifacts\package-' + [guid]::NewGuid().ToString('N'))
$releases = Join-Path $root 'releases'
$zip = Join-Path $releases "AC7-MOZA-FFB-$Version.zip"
if (Test-Path -LiteralPath $zip) { throw "Package already exists: $zip" }
New-Item -ItemType Directory -Path $stage, $releases -Force | Out-Null
try {
    foreach ($name in @('DCS.exe', 'Install.cmd', 'Uninstall.cmd', 'LICENSE', 'README.md', 'README_zh.md', 'payload', 'mod', 'presets', 'licenses')) {
        Copy-Item -LiteralPath (Join-Path $root $name) -Destination $stage -Recurse
    }
    $scripts = New-Item -ItemType Directory -Path (Join-Path $stage 'scripts')
    foreach ($name in @('Install.ps1', 'Uninstall.ps1', 'Common.ps1')) {
        Copy-Item (Join-Path $PSScriptRoot $name) $scripts.FullName
    }
    if (Test-Path -LiteralPath (Join-Path $root 'optional-logging')) {
        $logging = New-Item -ItemType Directory -Path (Join-Path $stage 'optional-logging')
        foreach ($name in @('AC7RumbleLog.exe', 'AC7IcingObserver.asi', 'README.md')) {
            Copy-Item (Join-Path $root ('optional-logging\' + $name)) $logging.FullName
        }
    }
    $files = @(Get-ChildItem -LiteralPath $stage -Recurse -File | ForEach-Object {
        [ordered]@{path = $_.FullName.Substring($stage.Length + 1).Replace('\', '/'); bytes = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
    })
    @{version = $Version; files = $files} | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $stage 'release.json') -Encoding UTF8
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip
    (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + (Split-Path -Leaf $zip) | Set-Content "$zip.sha256" -Encoding ASCII
    Write-Output "Packaged: $zip"
} finally { Remove-Item -LiteralPath $stage -Recurse -Force }
