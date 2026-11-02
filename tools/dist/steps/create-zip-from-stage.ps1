param (
    [Parameter(Mandatory = $true)][string]$StageDir,
    [Parameter(Mandatory = $true)][string]$ProjectName,
    [Parameter(Mandatory = $true)][string]$DistDir,
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$Suffix = ""
)

$ErrorActionPreference = "Stop"

New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
$ZipPath = Join-Path $DistDir "${ProjectName}-${Version}${Suffix}.zip"
if (Test-Path $ZipPath) {
    Remove-Item -Force $ZipPath
}

Compress-Archive -Path (Join-Path $StageDir "*") -CompressionLevel Optimal -DestinationPath $ZipPath
Write-Output $ZipPath
