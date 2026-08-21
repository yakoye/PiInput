param([ValidateSet("Debug", "Release")][string]$Configuration = "Release")
& (Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) "build.ps1") -Configuration $Configuration
exit $LASTEXITCODE
