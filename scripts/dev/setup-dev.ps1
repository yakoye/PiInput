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
# Two levels up: this script moved from the repository root into
# scripts/dev, and $Root still means the repository root.
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

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

$DistBin = Join-Path $Root "dist/windows-x64/bin"
$ProfileTool = Join-Path $DistBin "piinput-profile.exe"
$Installer = Join-Path $DistBin "PiInput-Install.exe"
foreach ($Required in @($ProfileTool, $Installer)) {
    if (-not (Test-Path $Required)) {
        throw "Missing Release output: $Required"
    }
}

& $ProfileTool --schema $Schema
if ($LASTEXITCODE -ne 0) {
    throw "Saving the PiInput schema failed with exit code $LASTEXITCODE."
}

if (-not $SkipDictionaryImport) {
    $ImportParameters = @{}
    if (Test-Path $ResolvedDictionaryDir) {
        $ImportParameters["DictionaryDir"] = $ResolvedDictionaryDir
    }
    & (Join-Path $PSScriptRoot "import-dicts.ps1") @ImportParameters
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

if (-not $SkipTsfRegistration) {
    $InstallerProcess = Start-Process -FilePath $Installer -ArgumentList "--silent" -Wait -PassThru
    if ($InstallerProcess.ExitCode -ne 0) {
        throw "PiInput-Install.exe failed with exit code $($InstallerProcess.ExitCode)."
    }
}

$VerifyParameters = @{}
if ($SkipTsfRegistration) {
    $VerifyParameters["SkipRegistryCheck"] = $true
}
& (Join-Path $PSScriptRoot "verify-windows.ps1") @VerifyParameters
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "PiInput developer setup completed." -ForegroundColor Green
if ($SkipTsfRegistration) {
    Write-Host "TSF installation was skipped. Run dist\windows-x64\bin\piinput-preview.exe for standalone testing." -ForegroundColor Cyan
} else {
    Write-Host "Open a new Notepad window, press Win+Space, and select PiInput." -ForegroundColor Cyan
}
exit 0
