param (
    [string]$ReleaseBin = "",
    [string]$ProjectName = "AnimgraphBridge",
    [ValidateSet("release", "releasedbg")]
    [string]$Mode = "release",
    [switch]$Build
)

$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
Set-Location $RepoRoot

if ($Build) {
    & xmake f -m $Mode
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & xmake
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

if (-not $ReleaseBin) {
    $ReleaseBin = "build/windows/x64/${Mode}/animgraph-bridge.dll"
}

if (-not (Test-Path $ReleaseBin)) {
    throw "Release binary not found: $ReleaseBin (pass -ReleaseBin or -Build)"
}

$StageDir = "build/package"
$DistDir = "build/dist"
$Version = & (Join-Path $PSScriptRoot "steps\get-version.ps1")

if (Test-Path $StageDir) {
    Remove-Item -Recurse -Force $StageDir
}

& (Join-Path $PSScriptRoot "steps\compose-red4ext.ps1") `
    -StageDir $StageDir `
    -ProjectName $ProjectName `
    -ReleaseBin $ReleaseBin

$ZipPath = & (Join-Path $PSScriptRoot "steps\create-zip-from-stage.ps1") `
    -StageDir $StageDir `
    -ProjectName $ProjectName `
    -DistDir $DistDir `
    -Version $Version

Remove-Item -Recurse -Force $StageDir

Write-Host "AnimgraphBridge: packaged → $ZipPath"
