Set-StrictMode -Version Latest

function Get-PiInputRegisteredDllPath {
    $clsid = "{13EB305F-2DA3-4CF7-8C45-16B016B801B5}"
    foreach ($keyPath in @(
        "HKCU:\Software\Classes\CLSID\$clsid\InprocServer32",
        "HKLM:\Software\Classes\CLSID\$clsid\InprocServer32"
    )) {
        $key = Get-Item -LiteralPath $keyPath -ErrorAction SilentlyContinue
        if ($key) {
            $value = [string]$key.GetValue("")
            if (-not [string]::IsNullOrWhiteSpace($value)) { return $value }
        }
    }
    return $null
}

function Get-PiInputCurrentHostPath {
    $key = Get-Item -LiteralPath "HKCU:\Software\PiInput\Runtime" -ErrorAction SilentlyContinue
    if (-not $key) { return $null }
    $value = [string]$key.GetValue("CurrentHostPath")
    if ([string]::IsNullOrWhiteSpace($value)) { return $null }
    return $value
}

function Get-PiInputPathComparison {
    if ([IO.Path]::DirectorySeparatorChar -eq '\') { return [StringComparison]::OrdinalIgnoreCase }
    return [StringComparison]::Ordinal
}

function Test-PiInputVersionName {
    param([Parameter(Mandatory = $true)][string]$VersionName)
    return $VersionName -ne "." -and $VersionName -ne ".." -and
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

function Resolve-PiInputRuntimeVersion {
    param(
        [Parameter(Mandatory = $true)][string]$PiInputRoot,
        [Parameter(Mandatory = $true)][string]$RuntimeRoot,
        [Parameter(Mandatory = $true)][string]$VersionsRoot,
        [Parameter(Mandatory = $true)][string]$VersionName,
        [Parameter(Mandatory = $true)][string]$Source,
        [string]$RegisteredDll = ""
    )
    if (-not (Test-PiInputVersionName -VersionName $VersionName)) {
        throw "PiInput version marker is not a single safe directory name."
    }
    $active = [IO.Path]::GetFullPath((Join-Path $VersionsRoot $VersionName))
    $bin = Join-Path $active "bin"
    $hostPath = Join-Path $bin "PiInputHost.exe"
    $profile = Join-Path $bin "piinput-profile.exe"
    $cli = Join-Path $bin "piinput-cli.exe"
    $shim = Join-Path $RuntimeRoot "Shim/PiInputTSF.dll"
    foreach ($required in @($PiInputRoot, $RuntimeRoot, $VersionsRoot, $active, $bin)) {
        if (-not (Test-Path -LiteralPath $required -PathType Container)) {
            throw "The PiInput runtime layout is incomplete: $required"
        }
        Assert-PiInputNotReparsePoint -Path $required -Label "PiInput runtime directory"
    }
    foreach ($required in @($hostPath, $profile, $cli, $shim)) {
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
            throw "The PiInput runtime file is missing: $required"
        }
        Assert-PiInputNotReparsePoint -Path $required -Label "PiInput runtime file"
    }
    if (-not [string]::IsNullOrWhiteSpace($RegisteredDll)) {
        $resolvedRegistered = [IO.Path]::GetFullPath($RegisteredDll)
        if (-not $resolvedRegistered.Equals(
                [IO.Path]::GetFullPath($shim), (Get-PiInputPathComparison))) {
            throw "The registered PiInput DLL is not the permanent stable Shim."
        }
    }
    [pscustomobject]@{
        RuntimeRoot = $RuntimeRoot
        DeveloperRoot = $RuntimeRoot
        VersionsRoot = $VersionsRoot
        ActiveVersionRoot = $active
        Bin = $bin
        Dll = $shim
        Host = $hostPath
        Profile = $profile
        Cli = $cli
        Source = $Source
    }
}

function Resolve-PiInputInstalledDev {
    param(
        [string]$LocalAppDataRoot = $env:LOCALAPPDATA,
        [scriptblock]$RegisteredDllPathProvider = { Get-PiInputRegisteredDllPath },
        [scriptblock]$CurrentHostPathProvider = { Get-PiInputCurrentHostPath }
    )
    if ([string]::IsNullOrWhiteSpace($LocalAppDataRoot)) { throw "LOCALAPPDATA is not available." }
    $root = [IO.Path]::GetFullPath((Join-Path $LocalAppDataRoot "PiInput"))
    $runtime = [IO.Path]::GetFullPath((Join-Path $root "Runtime"))
    $versions = [IO.Path]::GetFullPath((Join-Path $runtime "versions"))
    $markerPath = Join-Path $runtime "current.json"
    $registered = [string](& $RegisteredDllPathProvider)
    $markerFailure = $null

    if (Test-Path -LiteralPath $markerPath -PathType Leaf) {
        try {
            $marker = Get-Content -LiteralPath $markerPath -Raw | ConvertFrom-Json
            $protocolVersion = [int]$marker.protocol_version
            if ($null -eq $marker.version_id -or $protocolVersion -notin @(1, 2)) {
                throw "current.json has an unsupported protocol or missing version_id."
            }
            return Resolve-PiInputRuntimeVersion -PiInputRoot $root -RuntimeRoot $runtime `
                -VersionsRoot $versions -VersionName ([string]$marker.version_id) `
                -Source "current.json" -RegisteredDll $registered
        } catch {
            $markerFailure = $_.Exception.Message
        }
    }

    try {
        $currentHostPath = [string](& $CurrentHostPathProvider)
        if ([string]::IsNullOrWhiteSpace($currentHostPath) -or
            -not [IO.Path]::IsPathRooted($currentHostPath)) {
            throw "No absolute CurrentHostPath was found."
        }
        $resolvedHost = [IO.Path]::GetFullPath($currentHostPath)
        $prefix = $versions.TrimEnd([IO.Path]::DirectorySeparatorChar,
            [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
        if (-not $resolvedHost.StartsWith($prefix, (Get-PiInputPathComparison))) {
            throw "CurrentHostPath is outside the versions directory."
        }
        $segments = @(($resolvedHost.Substring($prefix.Length)) -split '[\\/]')
        if ($segments.Count -ne 3 -or $segments[1] -ne "bin" -or
            $segments[2] -ne "PiInputHost.exe") {
            throw "CurrentHostPath does not match <version>/bin/PiInputHost.exe."
        }
        return Resolve-PiInputRuntimeVersion -PiInputRoot $root -RuntimeRoot $runtime `
            -VersionsRoot $versions -VersionName $segments[0] -Source "registry" `
            -RegisteredDll $registered
    } catch {
        $fallbackFailure = $_.Exception.Message
        if ($markerFailure) {
            throw "PiInput current.json is invalid ($markerFailure) and registry fallback failed ($fallbackFailure)."
        }
        throw "PiInput current.json is missing and registry fallback failed ($fallbackFailure)."
    }
}
