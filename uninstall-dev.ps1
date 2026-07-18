param([switch]$RemoveUserData)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$Dev = Join-Path $env:LOCALAPPDATA "LiteIME/Dev"
$StartMenuDirectory = Join-Path $env:APPDATA "Microsoft/Windows/Start Menu/Programs/LiteIME"
$ProfileTool = Join-Path $Dev "bin/liteime-profile.exe"
$TsfDll = Join-Path $Dev "bin/LiteImeTSF.dll"
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

if (Test-Path $ProfileTool) {
    Invoke-NativeBestEffort -Description "Profile deactivation" -Command {
        & $ProfileTool --deactivate
    }
    Invoke-NativeBestEffort -Description "Profile unregistration" -Command {
        & $ProfileTool --unregister
    }
}
if (Test-Path $TsfDll) {
    Invoke-NativeBestEffort -Description "DLL unregistration" -Command {
        & $RegSvr32 /u /s $TsfDll
    }
}

if (Test-Path $Dev) {
    try {
        Remove-Item $Dev -Recurse -Force
    } catch {
        Write-Warning "Some applications still have LiteImeTSF.dll loaded. Sign out or restart Windows, then remove: $Dev"
    }
}
if (Test-Path $StartMenuDirectory) {
    Remove-Item $StartMenuDirectory -Recurse -Force
}
if ($RemoveUserData) {
    $UserData = Join-Path $env:LOCALAPPDATA "LiteIME/UserData"
    if (Test-Path $UserData) {
        Remove-Item $UserData -Recurse -Force
    }
}
Write-Host "LiteIME developer input method removed." -ForegroundColor Green
if (-not $RemoveUserData) {
    Write-Host "User dictionaries, settings, and learning data were preserved." -ForegroundColor Cyan
}
exit 0
