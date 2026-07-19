Set-StrictMode -Version Latest

function Get-PiInputRegisteredDllPath {
    $clsid = "{D73AABA7-BE3E-4E53-8DE2-652D352743F3}"
    foreach ($keyPath in @(
        "HKCU:\Software\Classes\CLSID\$clsid\InprocServer32",
        "HKLM:\Software\Classes\CLSID\$clsid\InprocServer32"
    )) {
        $key = Get-Item -LiteralPath $keyPath -ErrorAction SilentlyContinue
        if ($key) {
            $value = [string]$key.GetValue("")
            if (-not [string]::IsNullOrWhiteSpace($value)) {
                return $value
            }
        }
    }
    return $null
}

function Test-PiInputPathWithinRoot {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Root
    )
    $fullPath = [IO.Path]::GetFullPath($Path)
    $fullRoot = [IO.Path]::GetFullPath($Root).TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar
    ) + [IO.Path]::DirectorySeparatorChar
    $comparison = if ([IO.Path]::DirectorySeparatorChar -eq '\') {
        [StringComparison]::OrdinalIgnoreCase
    } else {
        [StringComparison]::Ordinal
    }
    return $fullPath.StartsWith($fullRoot, $comparison)
}

function Resolve-PiInputInstalledDev {
    param(
        [string]$LocalAppDataRoot = $env:LOCALAPPDATA,
        [scriptblock]$RegisteredDllPathProvider = { Get-PiInputRegisteredDllPath }
    )
    if ([string]::IsNullOrWhiteSpace($LocalAppDataRoot)) {
        throw "LOCALAPPDATA is not available."
    }

    $developerRoot = [IO.Path]::GetFullPath((Join-Path $LocalAppDataRoot "PiInput/Dev"))
    $versionsRoot = [IO.Path]::GetFullPath((Join-Path $developerRoot "versions"))
    $currentMarker = Join-Path $developerRoot "current.txt"
    $source = "current.txt"

    if (Test-Path -LiteralPath $currentMarker -PathType Leaf) {
        $markerValue = (Get-Content -LiteralPath $currentMarker -Raw).Trim()
        $isAbsolute = [IO.Path]::IsPathRooted($markerValue)
        if ([IO.Path]::DirectorySeparatorChar -eq '\') {
            $isAbsolute = $isAbsolute -and $markerValue -match '^(?:[A-Za-z]:[\\/]|\\\\[^\\]+\\[^\\]+)'
        }
        if ([string]::IsNullOrWhiteSpace($markerValue) -or -not $isAbsolute) {
            throw "PiInput current.txt does not contain an absolute version path."
        }
        $activeVersionRoot = [IO.Path]::GetFullPath($markerValue)
        if (-not (Test-PiInputPathWithinRoot -Path $activeVersionRoot -Root $versionsRoot)) {
            throw "PiInput current.txt points outside the versions directory."
        }
    } else {
        $registeredDll = & $RegisteredDllPathProvider
        if ([string]::IsNullOrWhiteSpace([string]$registeredDll)) {
            throw "PiInput current.txt is missing and no registered TSF DLL was found."
        }
        $registeredDll = [IO.Path]::GetFullPath([string]$registeredDll)
        if (-not (Test-PiInputPathWithinRoot -Path $registeredDll -Root $versionsRoot)) {
            throw "The registered PiInput TSF DLL is outside the versions directory."
        }
        $binFromRegistration = Split-Path -Parent $registeredDll
        $activeVersionRoot = Split-Path -Parent $binFromRegistration
        $source = "COM"
    }

    if (-not (Test-Path -LiteralPath $activeVersionRoot -PathType Container)) {
        throw "The active PiInput version directory does not exist: $activeVersionRoot"
    }
    $bin = Join-Path $activeVersionRoot "bin"
    $dll = Join-Path $bin "PiInputTSF.dll"
    $profile = Join-Path $bin "piinput-profile.exe"
    if (-not (Test-Path -LiteralPath $dll -PathType Leaf) -or
        -not (Test-Path -LiteralPath $profile -PathType Leaf)) {
        throw "The active PiInput version is incomplete: $activeVersionRoot"
    }

    [pscustomobject]@{
        DeveloperRoot = $developerRoot
        VersionsRoot = $versionsRoot
        ActiveVersionRoot = [IO.Path]::GetFullPath($activeVersionRoot)
        Bin = $bin
        Dll = $dll
        Profile = $profile
        Source = $source
    }
}
