param(
    [ValidateSet("full", "flypy", "natural", "mspy", "abc")]
    [string]$Schema = "flypy",
    [switch]$ImportScel,
    [string]$DictionaryDir = "",
    [switch]$SkipTsfRegistration
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
# Two levels up: this script moved from the repository root into
# scripts/dev, and $Root still means the repository root.
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Source = Join-Path $Root "dist/windows-x64"
$Destination = Join-Path $env:LOCALAPPDATA "PiInput/Dev"
$Preview = Join-Path $Destination "bin/piinput-preview.exe"
$ProfileTool = Join-Path $Destination "bin/piinput-profile.exe"
$TsfDll = Join-Path $Destination "bin/PiInputTSF.dll"
$StartMenuDirectory = Join-Path $env:APPDATA "Microsoft/Windows/Start Menu/Programs/PiInput"
$PreviewShortcut = Join-Path $StartMenuDirectory "PiInput Preview (Developer).lnk"
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
    (Join-Path $Source "bin/piinput-cli.exe"),
    (Join-Path $Source "bin/piinput-preview.exe"),
    (Join-Path $Source "bin/PiInput-Settings.exe"),
    (Join-Path $Source "bin/piinput-profile.exe"),
    (Join-Path $Source "bin/PiInputTSF.dll"),
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
$OldProfileTool = Join-Path $Destination "bin/piinput-profile.exe"
$OldTsfDll = Join-Path $Destination "bin/PiInputTSF.dll"
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

$UserDataDirectory = Join-Path $env:LOCALAPPDATA "PiInput/UserData"
$CandidateSettings = Join-Path $UserDataDirectory "settings.ini"
New-Item $UserDataDirectory -ItemType Directory -Force | Out-Null
$settingsLines = if (Test-Path $CandidateSettings) {
    @(Get-Content $CandidateSettings -Encoding UTF8)
} else {
    @()
}
$updatedSettings = @()
$inCandidates = $false
$foundCandidates = $false
$candidateKeys = @{
    items_per_row = $false
    visible_rows = $false
    max_items = $false
}
foreach ($line in $settingsLines) {
    if ($line -match '^\s*\[([^\]]+)\]\s*$') {
        if ($inCandidates) {
            if (-not $candidateKeys.items_per_row) { $updatedSettings += "items_per_row=6" }
            if (-not $candidateKeys.visible_rows) { $updatedSettings += "visible_rows=5" }
            if (-not $candidateKeys.max_items) { $updatedSettings += "max_items=90" }
        }
        $inCandidates = $Matches[1] -ieq "candidates"
        if ($inCandidates) {
            $foundCandidates = $true
            $candidateKeys.items_per_row = $false
            $candidateKeys.visible_rows = $false
            $candidateKeys.max_items = $false
        }
        $updatedSettings += $line
        continue
    }
    if ($line -match '^\s*(single_syllable_page_size|phrase_page_size)\s*=') {
        continue
    }
    if ($inCandidates -and $line -match '^\s*(items_per_row|visible_rows|max_items)\s*=') {
        $candidateKeys[$Matches[1]] = $true
    }
    $updatedSettings += $line
}
if ($inCandidates) {
    if (-not $candidateKeys.items_per_row) { $updatedSettings += "items_per_row=6" }
    if (-not $candidateKeys.visible_rows) { $updatedSettings += "visible_rows=5" }
    if (-not $candidateKeys.max_items) { $updatedSettings += "max_items=90" }
} elseif (-not $foundCandidates) {
    if ($updatedSettings.Count -gt 0 -and $updatedSettings[-1] -ne "") {
        $updatedSettings += ""
    }
    $updatedSettings += "[candidates]"
    $updatedSettings += "items_per_row=6"
    $updatedSettings += "visible_rows=5"
    $updatedSettings += "max_items=90"
}
$updatedSettings | Set-Content $CandidateSettings -Encoding UTF8

Invoke-NativeRequired -Description "Saving the PiInput input schema" -Command {
    & $ProfileTool --schema $Schema
}

if ($ImportScel) {
    $ImportParameters = @{}
    if (-not [string]::IsNullOrWhiteSpace($DictionaryDir)) {
        $ImportParameters.DictionaryDir = $DictionaryDir
    }
    & (Join-Path $PSScriptRoot "import-dicts.ps1") @ImportParameters
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

New-Item $StartMenuDirectory -ItemType Directory -Force | Out-Null
$Shell = New-Object -ComObject WScript.Shell
$Link = $Shell.CreateShortcut($PreviewShortcut)
$Link.TargetPath = $Preview
$Link.WorkingDirectory = Split-Path -Parent $Preview
$Link.Description = "PiInput input-core preview (developer build)"
$Link.Save()

if (-not $SkipTsfRegistration) {
    Invoke-NativeRequired -Description "Registering PiInputTSF.dll" -Command {
        & $RegSvr32 /s $TsfDll
    }
    Invoke-NativeRequired -Description "Registering the PiInput TSF profile" -Command {
        & $ProfileTool --register
    }
    Invoke-NativeRequired -Description "Activating the PiInput TSF profile" -Command {
        & $ProfileTool --activate
    }
    Invoke-NativeRequired -Description "Verifying the PiInput TSF profile" -Command {
        & $ProfileTool --status
    }
}

Write-Host "PiInput developer build installed to: $Destination" -ForegroundColor Green
Write-Host "Input schema: $Schema" -ForegroundColor Green
Write-Host "Start Menu shortcut created: $PreviewShortcut" -ForegroundColor Green
if (-not $SkipTsfRegistration) {
    $CtfMon = Join-Path $env:SystemRoot "System32/ctfmon.exe"
    Start-Process $CtfMon
    Write-Host "PiInput TSF system input method registered." -ForegroundColor Green
    Write-Host "Close and reopen Settings and Notepad, then press Win+Space and select 'PiInput 中文输入法'." -ForegroundColor Cyan
} else {
    Write-Host "TSF registration was skipped; only the preview and tools were installed." -ForegroundColor Yellow
}
exit 0
