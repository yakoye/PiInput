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

function Get-PiInputPathComparison {
    if ([IO.Path]::DirectorySeparatorChar -eq '\') {
        return [StringComparison]::OrdinalIgnoreCase
    }
    return [StringComparison]::Ordinal
}

function Test-PiInputVersionName {
    param([Parameter(Mandatory = $true)][string]$VersionName)
    return $VersionName -ne "." -and
        $VersionName -ne ".." -and
        $VersionName -match '^[A-Za-z0-9._-]+$'
}

function Assert-PiInputNotReparsePoint {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Label
    )
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Label must not be a reparse point: $Path"
    }
}

function Resolve-PiInputVersionLayout {
    param(
        [Parameter(Mandatory = $true)][string]$DeveloperRoot,
        [Parameter(Mandatory = $true)][string]$VersionsRoot,
        [Parameter(Mandatory = $true)][string]$VersionName,
        [Parameter(Mandatory = $true)][string]$Source
    )
    if (-not (Test-PiInputVersionName -VersionName $VersionName)) {
        throw "PiInput version marker is not a single safe directory name."
    }

    $activeVersionRoot = [IO.Path]::GetFullPath((Join-Path $VersionsRoot $VersionName))
    $bin = Join-Path $activeVersionRoot "bin"
    $dll = Join-Path $bin "PiInputTSF.dll"
    $profile = Join-Path $bin "piinput-profile.exe"
    if (-not (Test-Path -LiteralPath $VersionsRoot -PathType Container) -or
        -not (Test-Path -LiteralPath $activeVersionRoot -PathType Container) -or
        -not (Test-Path -LiteralPath $bin -PathType Container) -or
        -not (Test-Path -LiteralPath $dll -PathType Leaf) -or
        -not (Test-Path -LiteralPath $profile -PathType Leaf)) {
        throw "The PiInput version layout is missing or incomplete: $VersionName"
    }

    Assert-PiInputNotReparsePoint -Path $VersionsRoot -Label "PiInput versions directory"
    Assert-PiInputNotReparsePoint -Path $activeVersionRoot -Label "PiInput version directory"
    Assert-PiInputNotReparsePoint -Path $bin -Label "PiInput version bin directory"
    Assert-PiInputNotReparsePoint -Path $dll -Label "PiInput TSF DLL"
    Assert-PiInputNotReparsePoint -Path $profile -Label "PiInput profile tool"

    [pscustomobject]@{
        DeveloperRoot = $DeveloperRoot
        VersionsRoot = $VersionsRoot
        ActiveVersionRoot = $activeVersionRoot
        Bin = $bin
        Dll = $dll
        Profile = $profile
        Source = $Source
    }
}

function Resolve-PiInputRegisteredLayout {
    param(
        [Parameter(Mandatory = $true)][string]$DeveloperRoot,
        [Parameter(Mandatory = $true)][string]$VersionsRoot,
        [Parameter(Mandatory = $true)][string]$RegisteredDll
    )
    if (-not [IO.Path]::IsPathRooted($RegisteredDll)) {
        throw "The registered PiInput TSF path is not absolute."
    }
    $registeredDll = [IO.Path]::GetFullPath($RegisteredDll)
    $versionsPrefix = $VersionsRoot.TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar
    ) + [IO.Path]::DirectorySeparatorChar
    if (-not $registeredDll.StartsWith($versionsPrefix, (Get-PiInputPathComparison))) {
        throw "The registered PiInput TSF DLL is outside the versions directory."
    }

    $relativeDll = $registeredDll.Substring($versionsPrefix.Length)
    $segments = @($relativeDll -split '[\\/]')
    if ($segments.Count -ne 3 -or
        -not (Test-PiInputVersionName -VersionName $segments[0]) -or
        $segments[1] -ne "bin" -or
        $segments[2] -ne "PiInputTSF.dll") {
        throw "The registered PiInput TSF DLL does not match the required version/bin layout."
    }

    $layout = Resolve-PiInputVersionLayout `
        -DeveloperRoot $DeveloperRoot `
        -VersionsRoot $VersionsRoot `
        -VersionName $segments[0] `
        -Source "COM"
    if (-not $layout.Dll.Equals($registeredDll, (Get-PiInputPathComparison))) {
        throw "The registered PiInput TSF DLL does not match the resolved version layout."
    }
    return $layout
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
    $markerFailure = $null

    if (Test-Path -LiteralPath $currentMarker -PathType Leaf) {
        try {
            $markerValue = (Get-Content -LiteralPath $currentMarker -Raw).Trim()
            return Resolve-PiInputVersionLayout `
                -DeveloperRoot $developerRoot `
                -VersionsRoot $versionsRoot `
                -VersionName $markerValue `
                -Source "current.txt"
        } catch {
            $markerFailure = $_.Exception.Message
        }
    }

    try {
        $registeredDll = [string](& $RegisteredDllPathProvider)
        if ([string]::IsNullOrWhiteSpace($registeredDll)) {
            throw "No registered TSF DLL was found."
        }
        return Resolve-PiInputRegisteredLayout `
            -DeveloperRoot $developerRoot `
            -VersionsRoot $versionsRoot `
            -RegisteredDll $registeredDll
    } catch {
        $comFailure = $_.Exception.Message
        if ($markerFailure) {
            throw "PiInput current.txt is invalid ($markerFailure) and COM fallback failed ($comFailure)."
        }
        throw "PiInput current.txt is missing and COM fallback failed ($comFailure)."
    }
}
