param(
    [ValidateSet("Debug", "Release")][string]$Configuration = "Release",
    [string]$BuildDir = "",
    [string]$DistDir = "",
    [string]$ArtifactsDir = "",
    [string]$VersionLabel = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if ([string]::IsNullOrWhiteSpace($BuildDir)) { $BuildDir = Join-Path $Root "build/windows-x64" }
if ([string]::IsNullOrWhiteSpace($DistDir)) { $DistDir = Join-Path $Root "dist/windows-x64" }
if ([string]::IsNullOrWhiteSpace($ArtifactsDir)) { $ArtifactsDir = Join-Path $Root "artifacts" }
function Resolve-PackagePath([string]$Value) {
    $candidate = if ([IO.Path]::IsPathRooted($Value)) { $Value } else { Join-Path $Root $Value }
    return [IO.Path]::GetFullPath($candidate)
}
$BuildDir = Resolve-PackagePath $BuildDir
$DistDir = Resolve-PackagePath $DistDir
$ArtifactsDir = Resolve-PackagePath $ArtifactsDir
if ([string]::IsNullOrWhiteSpace($VersionLabel)) {
    $VersionLabel = (Get-Content -Raw -LiteralPath (Join-Path $Root "VERSION")).Trim()
}

$PackageName = "PiInput-$VersionLabel-portable-test-windows-x64"
$PackageRoot = Join-Path $ArtifactsDir $PackageName
$Zip = Join-Path $ArtifactsDir "$PackageName.zip"
$ReleaseBin = Join-Path $BuildDir $Configuration

$payload = [ordered]@{
    (Join-Path $DistDir "bin/piinput-preview.exe") = "PiInput-Test.exe"
    (Join-Path $ReleaseBin "piinput-host-session-tests.exe") = "PiInput-Command-SelfTest.exe"
    (Join-Path $DistDir "bin/piinput-cli.exe") = "piinput-cli.exe"
    (Join-Path $DistDir "bin/piinput-benchmark.exe") = "piinput-benchmark.exe"
    (Join-Path $Root "Run-Portable-Tests.cmd") = "Run-Portable-Tests.cmd"
    (Join-Path $Root "scripts/run-portable-tests.ps1") = "scripts/run-portable-tests.ps1"
    (Join-Path $Root "docs/PORTABLE_TEST_GUIDE.md") = "PORTABLE_TEST_GUIDE.md"
    (Join-Path $Root "docs/PORTABLE_TEST_RESULTS_2026-08-15.md") = "PORTABLE_TEST_RESULTS_2026-08-15.md"
}
foreach ($source in $payload.Keys) {
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Portable test payload is missing: $source"
    }
}
foreach ($requiredData in @(
    "piinput-base.lex",
    "symbols.tsv",
    "english_lexicon.tsv",
    "english_supplement.tsv",
    "english_completion_preferences.tsv"
)) {
    $source = Join-Path $DistDir "data/$requiredData"
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Portable test data is missing: $source"
    }
}

New-Item -ItemType Directory -Force $ArtifactsDir | Out-Null
if (Test-Path -LiteralPath $PackageRoot) { Remove-Item -LiteralPath $PackageRoot -Recurse -Force }
if (Test-Path -LiteralPath $Zip) { Remove-Item -LiteralPath $Zip -Force }
New-Item -ItemType Directory -Force $PackageRoot | Out-Null

foreach ($entry in $payload.GetEnumerator()) {
    $destination = Join-Path $PackageRoot $entry.Value
    New-Item -ItemType Directory -Force (Split-Path -Parent $destination) | Out-Null
    Copy-Item -LiteralPath $entry.Key -Destination $destination -Force
}
New-Item -ItemType Directory -Force (Join-Path $PackageRoot "data") | Out-Null
foreach ($requiredData in @(
    "piinput-base.lex",
    "symbols.tsv",
    "english_lexicon.tsv",
    "english_supplement.tsv",
    "english_completion_preferences.tsv"
)) {
    Copy-Item -LiteralPath (Join-Path $DistDir "data/$requiredData") `
        -Destination (Join-Path $PackageRoot "data/$requiredData") -Force
}

Compress-Archive -LiteralPath $PackageRoot -DestinationPath $Zip -CompressionLevel Optimal
$hash = (Get-FileHash -LiteralPath $Zip -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath "$Zip.sha256.txt" -Encoding ASCII `
    -Value "$hash  $([IO.Path]::GetFileName($Zip))"
Write-Host "Portable package: $Zip" -ForegroundColor Green
Write-Host "SHA-256: $hash" -ForegroundColor Green
