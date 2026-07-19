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
$normalVersionName = "0.3.0-dev-normal"
$normalVersion = New-VersionLayout -LocalRoot $normalLocal -VersionName $normalVersionName
$normalDev = Join-Path $normalLocal "PiInput/Dev"
Set-Content (Join-Path $normalDev "current.txt") -Value $normalVersionName -NoNewline
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

$invalidMarkerLocal = Join-Path $fixtureRoot "invalid-marker-fallback"
$invalidMarkerVersionName = "0.3.0-dev-com-fallback"
$invalidMarkerVersion = New-VersionLayout -LocalRoot $invalidMarkerLocal -VersionName $invalidMarkerVersionName
$invalidMarkerDev = Join-Path $invalidMarkerLocal "PiInput/Dev"
Set-Content (Join-Path $invalidMarkerDev "current.txt") -Value "../invalid" -NoNewline
$invalidMarkerDll = Join-Path $invalidMarkerVersion "bin/PiInputTSF.dll"
$invalidFallback = Resolve-PiInputInstalledDev -LocalAppDataRoot $invalidMarkerLocal -RegisteredDllPathProvider { $invalidMarkerDll }
if ($invalidFallback.Source -ne "COM" -or
    $invalidFallback.ActiveVersionRoot -ne [IO.Path]::GetFullPath($invalidMarkerVersion)) {
    throw "Invalid current.txt did not fall back to the validated COM layout."
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
New-Item (Join-Path $nonexistentLocal "PiInput/Dev") -ItemType Directory -Force | Out-Null
Set-Content (Join-Path $nonexistentLocal "PiInput/Dev/current.txt") -Value "0.3.0-dev-missing" -NoNewline
Assert-Throws -Label "missing active version" -Action {
    Resolve-PiInputInstalledDev -LocalAppDataRoot $nonexistentLocal -RegisteredDllPathProvider { $null }
}

$absoluteMarkerLocal = Join-Path $fixtureRoot "absolute-marker"
$absoluteMarkerVersion = New-VersionLayout -LocalRoot $absoluteMarkerLocal -VersionName "0.3.0-dev-absolute"
Set-Content (Join-Path $absoluteMarkerLocal "PiInput/Dev/current.txt") -Value $absoluteMarkerVersion -NoNewline
Assert-Throws -Label "absolute current marker" -Action {
    Resolve-PiInputInstalledDev -LocalAppDataRoot $absoluteMarkerLocal -RegisteredDllPathProvider { $null }
}

$wrongNameLocal = Join-Path $fixtureRoot "wrong-dll-name"
$wrongNameVersion = New-VersionLayout -LocalRoot $wrongNameLocal -VersionName "0.3.0-dev-wrong-name"
$wrongNameDll = Join-Path $wrongNameVersion "bin/not-the-tsf.dll"
Set-Content $wrongNameDll -Value "fixture" -NoNewline
Assert-Throws -Label "wrong COM DLL basename" -Action {
    Resolve-PiInputInstalledDev -LocalAppDataRoot $wrongNameLocal -RegisteredDllPathProvider { $wrongNameDll }
}

$wrongLayerLocal = Join-Path $fixtureRoot "wrong-layer"
$wrongLayerVersion = New-VersionLayout -LocalRoot $wrongLayerLocal -VersionName "parent/nested"
$wrongLayerDll = Join-Path $wrongLayerVersion "bin/PiInputTSF.dll"
Assert-Throws -Label "wrong COM directory depth" -Action {
    Resolve-PiInputInstalledDev -LocalAppDataRoot $wrongLayerLocal -RegisteredDllPathProvider { $wrongLayerDll }
}

$junctionLocal = Join-Path $fixtureRoot "junction"
$junctionVersionName = "0.3.0-dev-junction"
$junctionVersions = Join-Path $junctionLocal "PiInput/Dev/versions"
$junctionPath = Join-Path $junctionVersions $junctionVersionName
$junctionTarget = Join-Path $fixtureRoot "junction-target"
$junctionTargetBin = Join-Path $junctionTarget "bin"
New-Item $junctionVersions -ItemType Directory -Force | Out-Null
New-Item $junctionTargetBin -ItemType Directory -Force | Out-Null
Set-Content (Join-Path $junctionTargetBin "PiInputTSF.dll") -Value "fixture" -NoNewline
Set-Content (Join-Path $junctionTargetBin "piinput-profile.exe") -Value "fixture" -NoNewline
$junctionOutput = & $env:ComSpec /d /c "mklink /J `"$junctionPath`" `"$junctionTarget`"" 2>&1
$junctionCreated = $LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $junctionPath -PathType Container)
if ($junctionCreated) {
    Set-Content (Join-Path $junctionLocal "PiInput/Dev/current.txt") -Value $junctionVersionName -NoNewline
    Assert-Throws -Label "junction active version" -Action {
        Resolve-PiInputInstalledDev -LocalAppDataRoot $junctionLocal -RegisteredDllPathProvider { $null }
    }
    Write-Host "Reparse-point regression executed with a real junction."
} else {
    Write-Warning "SKIPPED reparse-point regression: junction creation failed: $junctionOutput"
}

Write-Host "Installed development resolver regression passed."
