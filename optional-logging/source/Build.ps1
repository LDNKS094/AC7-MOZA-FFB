$ErrorActionPreference = 'Stop'
$optional = Split-Path -Parent $PSScriptRoot
$root = Split-Path -Parent $optional
$exe = Join-Path $optional 'AC7RumbleLog.exe'
if (Get-Process AC7RumbleLog -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe }) { throw 'Stop this log tool before rebuilding it.' }
$build = Join-Path $root 'development\build\icing'
$publish = Join-Path $root 'development\artifacts\rumble-log'
& cmake -S (Join-Path $PSScriptRoot 'collector') -B $build -A x64
if ($LASTEXITCODE -ne 0) { throw 'Icing collector configure failed.' }
& cmake --build $build --config Release
if ($LASTEXITCODE -ne 0) { throw 'Icing collector build failed.' }
& dotnet publish (Join-Path $PSScriptRoot 'logger\AC7RumbleLog.csproj') -c Release -r win-x64 --self-contained false -p:PublishSingleFile=true -o $publish --verbosity minimal
if ($LASTEXITCODE -ne 0) { throw 'Logger publish failed.' }
Copy-Item (Join-Path $build 'Release\AC7IcingObserver.asi') $optional -Force
Copy-Item (Join-Path $publish 'AC7RumbleLog.exe') $exe -Force
Write-Output 'Built optional logging only. No main tool build, installation or device output.'
