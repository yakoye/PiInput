$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$uninstaller = Join-Path $repoRoot "scripts/dev/uninstall-dev.ps1"
$windowsPowerShell = (Get-Command powershell.exe -ErrorAction Stop).Source
$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ("piinput-uninstall-" + [Guid]::NewGuid().ToString("N"))
New-Item $fixtureRoot -ItemType Directory -Force | Out-Null

function Invoke-FailingUninstallCase {
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][scriptblock]$Arrange
    )
    $localRoot = Join-Path $fixtureRoot $Label
    $appDataRoot = Join-Path $localRoot "AppData/Roaming"
    $localAppDataRoot = Join-Path $localRoot "AppData/Local"
    $developerRoot = Join-Path $localAppDataRoot "PiInput/Dev"
    New-Item $developerRoot -ItemType Directory -Force | Out-Null
    $sentinel = Join-Path $developerRoot "must-remain.txt"
    Set-Content $sentinel -Value "preserve" -NoNewline
    & $Arrange $localAppDataRoot $developerRoot

    $savedLocalAppData = $env:LOCALAPPDATA
    $savedAppData = $env:APPDATA
    try {
        $env:LOCALAPPDATA = $localAppDataRoot
        $env:APPDATA = $appDataRoot
        $savedErrorAction = $ErrorActionPreference
        try {
            $ErrorActionPreference = "Continue"
            $uninstallOutput = & $windowsPowerShell -NoProfile -ExecutionPolicy Bypass -File $uninstaller 2>&1 | Out-String
            $exitCode = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = $savedErrorAction
        }
    } finally {
        $env:LOCALAPPDATA = $savedLocalAppData
        $env:APPDATA = $savedAppData
    }

    if ($exitCode -eq 0) {
        throw "$Label uninstall unexpectedly returned success.`n$uninstallOutput"
    }
    if (-not (Test-Path -LiteralPath $sentinel -PathType Leaf)) {
        throw "$Label uninstall deleted the development runtime."
    }
}

Invoke-FailingUninstallCase -Label "invalid-marker" -Arrange {
    param($localRoot, $developerRoot)
    Set-Content (Join-Path $developerRoot "current.txt") -Value "../invalid" -NoNewline
}

Invoke-FailingUninstallCase -Label "incomplete-version" -Arrange {
    param($localRoot, $developerRoot)
    $versionName = "0.3.0-dev-incomplete"
    New-Item (Join-Path $developerRoot "versions/$versionName/bin") -ItemType Directory -Force | Out-Null
    Set-Content (Join-Path $developerRoot "current.txt") -Value $versionName -NoNewline
    Set-Content (Join-Path $developerRoot "versions/$versionName/bin/PiInputTSF.dll") -Value "fixture" -NoNewline
}

Invoke-FailingUninstallCase -Label "profile-unregister-failure" -Arrange {
    param($localRoot, $developerRoot)
    $versionName = "0.3.0-dev-profile-failure"
    $bin = Join-Path $developerRoot "versions/$versionName/bin"
    New-Item $bin -ItemType Directory -Force | Out-Null
    Set-Content (Join-Path $developerRoot "current.txt") -Value $versionName -NoNewline
    Set-Content (Join-Path $bin "PiInputTSF.dll") -Value "fixture" -NoNewline
    Copy-Item (Join-Path $env:SystemRoot "System32/where.exe") (Join-Path $bin "piinput-profile.exe")
}

Write-Host "Uninstall fail-closed regression passed."
