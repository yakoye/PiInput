param([switch]$SkipBuild)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
# Two levels up: this script moved from the repository root into
# scripts/dev, and $Root still means the repository root.
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Source = Join-Path $Root "dist/windows-x64"
$SourceDll = Join-Path $Source "bin/PiInputTSF.dll"
$Installer = Join-Path $Source "bin/PiInput-Install.exe"
. (Join-Path $Root "scripts/windows/resolve-installed-dev.ps1")

if (-not $SkipBuild) {
    & (Join-Path $Root "build.ps1") -Configuration Release
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
if (-not (Test-Path -LiteralPath $SourceDll -PathType Leaf)) {
    throw "Current Release DLL is missing: $SourceDll"
}
if (-not (Test-Path -LiteralPath $Installer -PathType Leaf)) {
    throw "Current Release installer is missing: $Installer"
}

& $Installer --silent
if ($LASTEXITCODE -ne 0) {
    throw "PiInput side-by-side installer failed with exit code $LASTEXITCODE."
}

$Installed = Resolve-PiInputInstalledDev
$sourceHash = (Get-FileHash $SourceDll -Algorithm SHA256).Hash
$installedHash = (Get-FileHash $Installed.Dll -Algorithm SHA256).Hash
if ($sourceHash -ne $installedHash) {
    throw "Installed DLL hash does not match the current build."
}
Write-Host "PiInput current Release build was installed side by side." -ForegroundColor Green
Write-Host "Reopen the target application and switch to PiInput once." -ForegroundColor Cyan
