$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

# Two levels up: this script moved from the repository root into
# scripts/dev, and $Root still means the repository root.
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $Root "scripts/windows/resolve-installed-dev.ps1")
$Installed = Resolve-PiInputInstalledDev
$Dev = $Installed.DeveloperRoot
$Dll = $Installed.Dll
$Profile = $Installed.Profile
$RegSvr32 = Join-Path $env:SystemRoot "System32/regsvr32.exe"

function Invoke-NativeBestEffort {
    param(
        [Parameter(Mandatory = $true)]
        [scriptblock]$Command,
        [Parameter(Mandatory = $true)]
        [string]$Description
    )

    $PreviousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        & $Command 2>&1 | ForEach-Object { Write-Host $_ }
        $ExitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $PreviousPreference
    }

    if ($ExitCode -ne 0) {
        Write-Host "[INFO] $Description returned exit code $ExitCode; repair will continue." -ForegroundColor Yellow
    }
    return $ExitCode
}

function Invoke-NativeRequired {
    param(
        [Parameter(Mandatory = $true)]
        [scriptblock]$Command,
        [Parameter(Mandatory = $true)]
        [string]$Description
    )

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

Write-Host "[1/7] Deactivating any old PiInput profile (absence is allowed)..." -ForegroundColor Cyan
Invoke-NativeBestEffort -Description "Old profile deactivation" -Command {
    & $Profile --deactivate
} | Out-Null

Write-Host "[2/7] Unregistering any old DLL registration (absence is allowed)..." -ForegroundColor Cyan
Invoke-NativeBestEffort -Description "Old DLL unregistration" -Command {
    & $RegSvr32 /u /s $Dll
} | Out-Null

Write-Host "[3/7] Registering PiInputTSF.dll..." -ForegroundColor Cyan
Invoke-NativeRequired -Description "regsvr32 registration" -Command {
    & $RegSvr32 /s $Dll
}

Write-Host "[4/7] Registering and enabling the TSF profile explicitly..." -ForegroundColor Cyan
Invoke-NativeRequired -Description "PiInput profile registration" -Command {
    & $Profile --register
}

Write-Host "[5/7] Adding PiInput to the current user keyboard list..." -ForegroundColor Cyan
Invoke-NativeRequired -Description "PiInput current-user keyboard registration" -Command {
    & $Profile --enable-user
}

Write-Host "[6/7] Activating the PiInput profile..." -ForegroundColor Cyan
Invoke-NativeRequired -Description "PiInput profile activation" -Command {
    & $Profile --activate
}

Write-Host "[7/7] Verifying profile status..." -ForegroundColor Cyan
Invoke-NativeRequired -Description "PiInput profile verification" -Command {
    & $Profile --status
}

$CtfMon = Join-Path $env:SystemRoot "System32/ctfmon.exe"
Get-Process ctfmon -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Process $CtfMon

Write-Host "PiInput registration repaired." -ForegroundColor Green
Write-Host "Close and reopen Settings and Notepad, then use Win+Space." -ForegroundColor Green
exit 0
