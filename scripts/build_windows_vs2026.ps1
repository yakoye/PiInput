param(
    [ValidateSet("Debug", "Release")][string]$Configuration = "Release",
    [string]$TestDataDir = "",
    [switch]$Clean
)
& (Join-Path (Split-Path -Parent $PSScriptRoot) "build.ps1") -Configuration $Configuration -TestDataDir $TestDataDir -Clean:$Clean
exit $LASTEXITCODE
