$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

. (Join-Path $PSScriptRoot "../scripts/windows/resolve-installed-dev.ps1")

function Assert-Throws {
    param(
        [Parameter(Mandatory = $true)][scriptblock]$Action,
        [Parameter(Mandatory = $true)][string]$Label
    )
    try {
        & $Action
    } catch {
        return
    }
    throw "$Label did not reject the invalid layout."
}

function New-VersionLayout {
    param(
        [Parameter(Mandatory = $true)][string]$LocalRoot,
        [Parameter(Mandatory = $true)][string]$VersionName
    )
    $versionRoot = Join-Path $LocalRoot "PiInput/Dev/versions/$VersionName"
    $bin = Join-Path $versionRoot "bin"
    New-Item $bin -ItemType Directory -Force | Out-Null
    Set-Content (Join-Path $bin "PiInputTSF.dll") -Value "fixture" -NoNewline
    Set-Content (Join-Path $bin "piinput-profile.exe") -Value "fixture" -NoNewline
    return $versionRoot
}

$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ("piinput-resolver-" + [Guid]::NewGuid().ToString("N"))
New-Item $fixtureRoot -ItemType Directory -Force | Out-Null

$normalLocal = Join-Path $fixtureRoot "normal"
$normalVersion = New-VersionLayout -LocalRoot $normalLocal -VersionName "0.3.0-dev-normal"
$normalDev = Join-Path $normalLocal "PiInput/Dev"
Set-Content (Join-Path $normalDev "current.txt") -Value $normalVersion -NoNewline
$normal = Resolve-PiInputInstalledDev -LocalAppDataRoot $normalLocal -RegisteredDllPathProvider { $null }
if ($normal.ActiveVersionRoot -ne [IO.Path]::GetFullPath($normalVersion)) {
    throw "current.txt did not resolve the active version."
}

$fallbackLocal = Join-Path $fixtureRoot "fallback"
$fallbackVersion = New-VersionLayout -LocalRoot $fallbackLocal -VersionName "0.3.0-dev-fallback"
$fallbackDll = Join-Path $fallbackVersion "bin/PiInputTSF.dll"
$fallback = Resolve-PiInputInstalledDev -LocalAppDataRoot $fallbackLocal -RegisteredDllPathProvider { $fallbackDll }
if ($fallback.Source -ne "COM" -or $fallback.ActiveVersionRoot -ne [IO.Path]::GetFullPath($fallbackVersion)) {
    throw "COM registration did not resolve the active version."
}

$missingLocal = Join-Path $fixtureRoot "missing-marker"
New-Item (Join-Path $missingLocal "PiInput/Dev/versions") -ItemType Directory -Force | Out-Null
Assert-Throws -Label "missing current marker" -Action {
    Resolve-PiInputInstalledDev -LocalAppDataRoot $missingLocal -RegisteredDllPathProvider { $null }
}

$outsideLocal = Join-Path $fixtureRoot "outside"
$outsideVersion = New-VersionLayout -LocalRoot $fixtureRoot -VersionName "outside-version"
New-Item (Join-Path $outsideLocal "PiInput/Dev") -ItemType Directory -Force | Out-Null
Set-Content (Join-Path $outsideLocal "PiInput/Dev/current.txt") -Value $outsideVersion -NoNewline
Assert-Throws -Label "out-of-bounds current marker" -Action {
    Resolve-PiInputInstalledDev -LocalAppDataRoot $outsideLocal -RegisteredDllPathProvider { $null }
}

$nonexistentLocal = Join-Path $fixtureRoot "nonexistent"
$missingVersion = Join-Path $nonexistentLocal "PiInput/Dev/versions/0.3.0-dev-missing"
New-Item (Join-Path $nonexistentLocal "PiInput/Dev") -ItemType Directory -Force | Out-Null
Set-Content (Join-Path $nonexistentLocal "PiInput/Dev/current.txt") -Value $missingVersion -NoNewline
Assert-Throws -Label "missing active version" -Action {
    Resolve-PiInputInstalledDev -LocalAppDataRoot $nonexistentLocal -RegisteredDllPathProvider { $null }
}

Write-Host "Installed development resolver regression passed."
