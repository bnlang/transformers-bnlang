param(
    [string]$Preset = "windows-x64",
    [switch]$Clean,
    [switch]$Configure
)

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

$buildDir = Join-Path $PSScriptRoot "build/$Preset"

if ($Clean) {
    if (Test-Path $buildDir) { Remove-Item -Recurse -Force $buildDir }
    Write-Host "cleaned $buildDir" -ForegroundColor Yellow
    return
}

if ($Configure -or -not (Test-Path (Join-Path $buildDir 'CMakeCache.txt'))) {
    Write-Host "configuring preset: $Preset" -ForegroundColor Cyan
    & cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
}

Write-Host "building: $Preset" -ForegroundColor Cyan
& cmake --build $buildDir --config Release
if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }

Write-Host "built: $buildDir" -ForegroundColor Green
