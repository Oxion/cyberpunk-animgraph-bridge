$ErrorActionPreference = "Stop"

$fromHeader = Select-String -Path "src/App/Project.hpp" -Pattern '"(\d+\.\d+\.\d+)"' -List -ErrorAction SilentlyContinue
if ($fromHeader) {
    Write-Output $fromHeader.Matches.Groups[1].Value
    return
}

$fromXmake = Select-String -Path "xmake.lua" -Pattern 'set_version\("(\d+\.\d+\.\d+)"' -List
if (-not $fromXmake) {
    throw "Could not resolve version from src/App/Project.hpp or xmake.lua"
}
Write-Output $fromXmake.Matches.Groups[1].Value
