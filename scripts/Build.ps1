$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'DCS.exe'
if (Get-Process DCS -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe }) { throw 'Stop this DCS bridge before rebuilding it.' }
$publish = Join-Path $root 'development\artifacts\bridge'
& dotnet publish (Join-Path $root 'src\bridge\AceBridge.csproj') -c Release -r win-x64 --self-contained false -p:PublishSingleFile=true -o $publish --verbosity minimal
if ($LASTEXITCODE -ne 0) { throw 'Bridge publish failed.' }
foreach ($module in @(@{Source = 'rumble'; File = 'AC7RumbleTrace.asi'}, @{Source = 'input'; File = 'AC7ExpandedInput.asi'})) {
    $source = Join-Path $root ('src\' + $module.Source)
    $build = Join-Path $root ('development\build\' + $module.Source)
    & cmake -S $source -B $build -A x64
    if ($LASTEXITCODE -ne 0) { throw "Configure failed: $($module.Source)" }
    & cmake --build $build --config Release
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $($module.Source)" }
    New-Item -ItemType Directory -Path (Join-Path $root 'payload') -Force | Out-Null
    Copy-Item (Join-Path $build ('Release\' + $module.File)) (Join-Path $root 'payload') -Force
}
Copy-Item (Join-Path $publish 'DCS.exe') $exe -Force
Write-Output 'Built DCS.exe and payload/. No game installation, preset changes or device output.'
