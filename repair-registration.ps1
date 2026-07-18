$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$Dev = Join-Path $env:LOCALAPPDATA "LiteIME/Dev"
$Dll = Join-Path $Dev "bin/LiteImeTSF.dll"
$Profile = Join-Path $Dev "bin/liteime-profile.exe"
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

if (-not (Test-Path $Dll) -or -not (Test-Path $Profile)) {
    throw "LiteIME TSF files are not installed. Run .\setup-dev.cmd first."
}

Write-Host "[1/6] Deactivating any old LiteIME profile (absence is allowed)..." -ForegroundColor Cyan
Invoke-NativeBestEffort -Description "Old profile deactivation" -Command {
    & $Profile --deactivate
} | Out-Null

Write-Host "[2/6] Unregistering any old DLL registration (absence is allowed)..." -ForegroundColor Cyan
Invoke-NativeBestEffort -Description "Old DLL unregistration" -Command {
    & $RegSvr32 /u /s $Dll
} | Out-Null

Write-Host "[3/6] Registering LiteImeTSF.dll..." -ForegroundColor Cyan
Invoke-NativeRequired -Description "regsvr32 registration" -Command {
    & $RegSvr32 /s $Dll
}

Write-Host "[4/6] Registering and enabling the TSF profile explicitly..." -ForegroundColor Cyan
Invoke-NativeRequired -Description "LiteIME profile registration" -Command {
    & $Profile --register
}

Write-Host "[5/6] Activating the LiteIME profile..." -ForegroundColor Cyan
Invoke-NativeRequired -Description "LiteIME profile activation" -Command {
    & $Profile --activate
}

Write-Host "[6/6] Verifying profile status..." -ForegroundColor Cyan
Invoke-NativeRequired -Description "LiteIME profile verification" -Command {
    & $Profile --status
}

$CtfMon = Join-Path $env:SystemRoot "System32/ctfmon.exe"
Get-Process ctfmon -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Process $CtfMon

Write-Host "LiteIME registration repaired." -ForegroundColor Green
Write-Host "Close and reopen Settings and Notepad, then use Win+Space." -ForegroundColor Green
exit 0
