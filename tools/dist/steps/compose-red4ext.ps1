param (
    [Parameter(Mandatory = $true)][string]$StageDir,
    [Parameter(Mandatory = $true)][string]$ReleaseBin,
    [Parameter(Mandatory = $true)][string]$ProjectName,
    [string]$ConfigJson = "config/config.json"
)

$ErrorActionPreference = "Stop"

$PluginDir = Join-Path $StageDir "red4ext/plugins/${ProjectName}"
New-Item -ItemType Directory -Force -Path $PluginDir | Out-Null

Copy-Item -Path $ReleaseBin -Destination (Join-Path $PluginDir "animgraph-bridge.dll") -Force

if (-not (Test-Path $ConfigJson)) {
    throw "Missing config: $ConfigJson"
}
Copy-Item -Path $ConfigJson -Destination (Join-Path $PluginDir "config.json") -Force
