param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [ValidateSet("full", "flypy", "natural", "mspy", "abc")]
    [string]$Schema = "flypy",
    [string]$DictionaryDir = "",
    [switch]$NoClean,
    [switch]$SkipDictionaryImport,
    [switch]$SkipTsfRegistration
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$Root = $PSScriptRoot

$ResolvedDictionaryDir = $DictionaryDir
if ([string]::IsNullOrWhiteSpace($ResolvedDictionaryDir)) {
    $ResolvedDictionaryDir = Join-Path (Split-Path -Parent $Root) "dicts"
}

$BuildParameters = @{ Configuration = $Configuration }
if (-not $NoClean) {
    $BuildParameters["Clean"] = $true
}
if (Test-Path $ResolvedDictionaryDir) {
    $BuildParameters["TestDataDir"] = $ResolvedDictionaryDir
}

& (Join-Path $Root "build.ps1") @BuildParameters
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$InstallParameters = @{
    Schema = $Schema
    ImportScel = (-not $SkipDictionaryImport)
    SkipTsfRegistration = $SkipTsfRegistration
}
if (Test-Path $ResolvedDictionaryDir) {
    $InstallParameters["DictionaryDir"] = $ResolvedDictionaryDir
}
& (Join-Path $Root "install-dev.ps1") @InstallParameters
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$VerifyParameters = @{}
if ($SkipTsfRegistration) {
    $VerifyParameters["SkipRegistryCheck"] = $true
}
& (Join-Path $Root "verify-windows.ps1") @VerifyParameters
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "LiteIME developer setup completed." -ForegroundColor Green
if ($SkipTsfRegistration) {
    Write-Host "Run .\start-preview.cmd to test the standalone preview." -ForegroundColor Cyan
} else {
    Write-Host "Open a new Notepad window, press Win+Space, and select LiteIME." -ForegroundColor Cyan
}
exit 0
