param(
    [ValidateSet("full", "flypy", "natural", "mspy", "abc")]
    [string]$Schema = "flypy",
    [switch]$ImportScel,
    [string]$DictionaryDir = "",
    [switch]$SkipTsfRegistration
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$Root = $PSScriptRoot
$Source = Join-Path $Root "dist/windows-x64"
$Destination = Join-Path $env:LOCALAPPDATA "LiteIME/Dev"
$Preview = Join-Path $Destination "bin/liteime-preview.exe"
$ProfileTool = Join-Path $Destination "bin/liteime-profile.exe"
$TsfDll = Join-Path $Destination "bin/LiteImeTSF.dll"
$StartMenuDirectory = Join-Path $env:APPDATA "Microsoft/Windows/Start Menu/Programs/LiteIME"
$PreviewShortcut = Join-Path $StartMenuDirectory "LiteIME Preview (Developer).lnk"
$RegSvr32 = Join-Path $env:SystemRoot "System32/regsvr32.exe"

function Invoke-NativeBestEffort {
    param([scriptblock]$Command, [string]$Description)
    $PreviousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        & $Command 2>&1 | ForEach-Object { Write-Host $_ }
        $ExitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $PreviousPreference
    }
    if ($ExitCode -ne 0) {
        Write-Host "[INFO] $Description returned exit code $ExitCode; installation will continue." -ForegroundColor Yellow
    }
    return $ExitCode
}

function Invoke-NativeRequired {
    param([scriptblock]$Command, [string]$Description)
    $PreviousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        & $Command 2>&1 | ForEach-Object { Write-Host $_ }
        $ExitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $PreviousPreference
    }
    if ($ExitCode -ne 0) {
        throw "$Description failed with exit code $ExitCode."
    }
}

foreach ($required in @(
    (Join-Path $Source "bin/liteime-cli.exe"),
    (Join-Path $Source "bin/liteime-preview.exe"),
    (Join-Path $Source "bin/liteime-profile.exe"),
    (Join-Path $Source "bin/LiteImeTSF.dll"),
    (Join-Path $Source "data/base_lexicon.tsv"),
    (Join-Path $Source "data/symbols.tsv")
)) {
    if (-not (Test-Path $required)) {
        throw "Windows build output is missing: $required"
    }
}

# Ask the current-user TSF host to release the old developer DLL before replacement.
Get-Process ctfmon -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue

# Cleanup is deliberately idempotent. A missing or inactive previous profile is normal.
$OldProfileTool = Join-Path $Destination "bin/liteime-profile.exe"
$OldTsfDll = Join-Path $Destination "bin/LiteImeTSF.dll"
if (Test-Path $OldProfileTool) {
    Invoke-NativeBestEffort -Description "Previous profile deactivation" -Command {
        & $OldProfileTool --deactivate
    } | Out-Null
}
if (Test-Path $OldTsfDll) {
    Invoke-NativeBestEffort -Description "Previous DLL unregistration" -Command {
        & $RegSvr32 /u /s $OldTsfDll
    } | Out-Null
}

if (Test-Path $Destination) {
    Remove-Item $Destination -Recurse -Force
}
New-Item $Destination -ItemType Directory -Force | Out-Null
Copy-Item (Join-Path $Source "*") $Destination -Recurse -Force
$Version = (Get-Content (Join-Path $Root "VERSION") -Raw).Trim()
Set-Content (Join-Path $Destination "VERSION") $Version -Encoding UTF8

$UserDataDirectory = Join-Path $env:LOCALAPPDATA "LiteIME/UserData"
$CandidateSettings = Join-Path $UserDataDirectory "settings.ini"
New-Item $UserDataDirectory -ItemType Directory -Force | Out-Null
$settingsLines = if (Test-Path $CandidateSettings) { @(Get-Content $CandidateSettings) } else { @() }
if (-not ($settingsLines -match '^single_syllable_page_size=')) {
    $settingsLines += "single_syllable_page_size=9"
}
if (-not ($settingsLines -match '^phrase_page_size=')) {
    $settingsLines += "phrase_page_size=6"
}
$settingsLines | Set-Content $CandidateSettings -Encoding ASCII

Invoke-NativeRequired -Description "Saving the LiteIME input schema" -Command {
    & $ProfileTool --schema $Schema
}

if ($ImportScel) {
    $ImportParameters = @{}
    if (-not [string]::IsNullOrWhiteSpace($DictionaryDir)) {
        $ImportParameters.DictionaryDir = $DictionaryDir
    }
    & (Join-Path $Root "import-dicts.ps1") @ImportParameters
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

New-Item $StartMenuDirectory -ItemType Directory -Force | Out-Null
$Shell = New-Object -ComObject WScript.Shell
$Link = $Shell.CreateShortcut($PreviewShortcut)
$Link.TargetPath = $Preview
$Link.WorkingDirectory = Split-Path -Parent $Preview
$Link.Description = "LiteIME input-core preview (developer build)"
$Link.Save()

if (-not $SkipTsfRegistration) {
    Invoke-NativeRequired -Description "Registering LiteImeTSF.dll" -Command {
        & $RegSvr32 /s $TsfDll
    }
    Invoke-NativeRequired -Description "Registering the LiteIME TSF profile" -Command {
        & $ProfileTool --register
    }
    Invoke-NativeRequired -Description "Activating the LiteIME TSF profile" -Command {
        & $ProfileTool --activate
    }
    Invoke-NativeRequired -Description "Verifying the LiteIME TSF profile" -Command {
        & $ProfileTool --status
    }
}

Write-Host "LiteIME developer build installed to: $Destination" -ForegroundColor Green
Write-Host "Input schema: $Schema" -ForegroundColor Green
Write-Host "Start Menu shortcut created: $PreviewShortcut" -ForegroundColor Green
if (-not $SkipTsfRegistration) {
    $CtfMon = Join-Path $env:SystemRoot "System32/ctfmon.exe"
    Start-Process $CtfMon
    Write-Host "LiteIME TSF system input method registered." -ForegroundColor Green
    Write-Host "Close and reopen Settings and Notepad, then press Win+Space and select 'LiteIME 中文输入法（开发版）'." -ForegroundColor Cyan
} else {
    Write-Host "TSF registration was skipped; only the preview and tools were installed." -ForegroundColor Yellow
}
exit 0
