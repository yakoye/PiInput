param([switch]$RemoveUserData)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$Dev = Join-Path $env:LOCALAPPDATA "PiInput/Dev"
$StartMenuDirectory = Join-Path $env:APPDATA "Microsoft/Windows/Start Menu/Programs/PiInput"
# Two levels up: this script moved from the repository root into
# scripts/dev, and $Root still means the repository root.
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $Root "scripts/windows/resolve-installed-dev.ps1")
$Installed = Resolve-PiInputInstalledDev
$ProfileTool = $Installed.Profile
$TsfDll = $Installed.Dll
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
        Write-Host "[INFO] $Description returned exit code $ExitCode; uninstall will continue." -ForegroundColor Yellow
    }
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
        throw "$Description failed with exit code $ExitCode. The PiInput runtime was preserved."
    }
}

if (Test-Path -LiteralPath $ProfileTool -PathType Leaf) {
    Invoke-NativeBestEffort -Description "Current-user keyboard list removal" -Command {
        & $ProfileTool --disable-user
    }
    Invoke-NativeBestEffort -Description "Profile deactivation" -Command {
        & $ProfileTool --deactivate
    }
    Invoke-NativeRequired -Description "Profile unregistration" -Command {
        & $ProfileTool --unregister
    }
}
if (Test-Path -LiteralPath $TsfDll -PathType Leaf) {
    Invoke-NativeRequired -Description "DLL unregistration" -Command {
        & $RegSvr32 /u /s $TsfDll
    }
}

if (Test-Path $Dev) {
    Remove-Item $Dev -Recurse -Force
}
if (Test-Path $StartMenuDirectory) {
    Remove-Item $StartMenuDirectory -Recurse -Force
}
if ($RemoveUserData) {
    $UserData = Join-Path $env:LOCALAPPDATA "PiInput/UserData"
    if (Test-Path $UserData) {
        Remove-Item $UserData -Recurse -Force
    }
}
Write-Host "PiInput developer input method removed." -ForegroundColor Green
if (-not $RemoveUserData) {
    Write-Host "User dictionaries, settings, and learning data were preserved." -ForegroundColor Cyan
}
exit 0
