Set-StrictMode -Version Latest

# PiInput installs into one fixed directory. The layout this resolver used to
# describe -- a permanent Shim under Runtime\Shim and a per-version tree beside
# it -- was abandoned because a registration captured while a versioned path was
# current became a dead link the moment that version was replaced.
# platform/windows/installer/stable_runtime.cpp is the authority for what
# replaced it, and this file must not drift from it again:
#
#   %LOCALAPPDATA%\PiInput\current.json                   which build is installed
#   %LOCALAPPDATA%\PiInput\bin\                           Host, profile tool, CLI, user Shim
#   %ProgramFiles%\PiInput\Runtime\Shim\PiInputTSF.dll    the registered Shim
#
# No path below is derived from the version marker. The marker records which
# build is installed, not where it lives, and resolve_current_host in
# stable_runtime.cpp deliberately reads the fixed path rather than trust it.

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

function Get-PiInputMachineShimPath {
    param([Parameter(Mandatory = $true)][string]$ProgramFilesRoot)
    # machine_shim_path in stable_runtime.cpp. COM activation points here rather
    # than into the per-user tree because packaged Windows surfaces such as
    # SearchHost ignore the per-user COM class table.
    return [IO.Path]::GetFullPath(
        (Join-Path $ProgramFilesRoot "PiInput/Runtime/Shim/PiInputTSF.dll"))
}

function Get-PiInputPathComparison {
    if ([IO.Path]::DirectorySeparatorChar -eq '\') { return [StringComparison]::OrdinalIgnoreCase }
    return [StringComparison]::Ordinal
}

function Test-PiInputSamePath {
    param(
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$Left,
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$Right
    )
    if ([string]::IsNullOrWhiteSpace($Left) -or [string]::IsNullOrWhiteSpace($Right)) {
        return $false
    }
    return [IO.Path]::GetFullPath($Left).Equals(
        [IO.Path]::GetFullPath($Right), (Get-PiInputPathComparison))
}

function Test-PiInputVersionId {
    param([Parameter(Mandatory = $true)][AllowEmptyString()][string]$VersionId)
    # Mirrors safe_version_id in stable_runtime.cpp. The marker no longer names a
    # directory, so this detects a corrupt marker rather than a traversal, but it
    # stays aligned with the writer so the two cannot disagree about validity.
    return $VersionId -ne "." -and $VersionId -ne ".." -and
        $VersionId -match '^[A-Za-z0-9._-]+$'
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

function Resolve-PiInputRuntimeLayout {
    param(
        [Parameter(Mandatory = $true)][string]$PiInputRoot,
        [Parameter(Mandatory = $true)][string]$ProgramFilesRoot,
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$VersionId,
        [Parameter(Mandatory = $true)][string]$Source,
        [string]$RegisteredDll = ""
    )
    $root = [IO.Path]::GetFullPath($PiInputRoot)
    $bin = Join-Path $root "bin"
    $data = Join-Path $root "data"
    $hostPath = Join-Path $bin "PiInputHost.exe"
    $profile = Join-Path $bin "piinput-profile.exe"
    $cli = Join-Path $bin "piinput-cli.exe"
    $userShim = Join-Path $bin "PiInputTSF.dll"
    $machineShim = Get-PiInputMachineShimPath -ProgramFilesRoot $ProgramFilesRoot

    foreach ($required in @($root, $bin)) {
        if (-not (Test-Path -LiteralPath $required -PathType Container)) {
            throw "The PiInput runtime layout is incomplete: $required"
        }
        Assert-PiInputNotReparsePoint -Path $required -Label "PiInput runtime directory"
    }
    foreach ($required in @($hostPath, $profile, $cli, $userShim)) {
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
            throw "The PiInput runtime file is missing: $required"
        }
        Assert-PiInputNotReparsePoint -Path $required -Label "PiInput runtime file"
    }
    # The Shim the developer scripts hand to regsvr32 has to be the machine-wide
    # one: DllRegisterServer records whichever module regsvr32 loaded, so
    # registering the per-user copy would leave HKCU pointing somewhere the
    # installer asserts it never should.
    if (-not (Test-Path -LiteralPath $machineShim -PathType Leaf)) {
        throw ("The machine-wide PiInput Shim is missing: $machineShim. " +
            "Run PiInput-Install.exe once to restore machine COM activation.")
    }
    Assert-PiInputNotReparsePoint -Path $machineShim -Label "PiInput machine Shim"
    if (-not [string]::IsNullOrWhiteSpace($RegisteredDll) -and
        -not (Test-PiInputSamePath -Left $RegisteredDll -Right $machineShim)) {
        throw "The registered PiInput DLL is not the machine-wide stable Shim."
    }

    [pscustomobject]@{
        RuntimeRoot = $root
        DeveloperRoot = $bin
        Bin = $bin
        Data = $data
        Dll = $machineShim
        UserShim = $userShim
        Host = $hostPath
        Profile = $profile
        Cli = $cli
        VersionId = $VersionId
        Source = $Source
    }
}

function Resolve-PiInputInstalledDev {
    param(
        [string]$LocalAppDataRoot = $env:LOCALAPPDATA,
        [string]$ProgramFilesRoot = $env:ProgramFiles,
        [scriptblock]$RegisteredDllPathProvider = { Get-PiInputRegisteredDllPath },
        [scriptblock]$CurrentHostPathProvider = { Get-PiInputCurrentHostPath }
    )
    if ([string]::IsNullOrWhiteSpace($LocalAppDataRoot)) { throw "LOCALAPPDATA is not available." }
    if ([string]::IsNullOrWhiteSpace($ProgramFilesRoot)) { throw "ProgramFiles is not available." }
    $root = [IO.Path]::GetFullPath((Join-Path $LocalAppDataRoot "PiInput"))
    $markerPath = Join-Path $root "current.json"
    $expectedHost = [IO.Path]::GetFullPath((Join-Path $root "bin/PiInputHost.exe"))
    $registered = [string](& $RegisteredDllPathProvider)

    # Identity first, layout second. Because nothing is built from the marker,
    # a marker this resolver cannot read costs the version string and nothing
    # else, and the layout below is checked exactly once instead of being
    # retried -- and re-reported -- for every source that failed.
    $source = $null
    $versionId = ""
    $failures = @()

    if (Test-Path -LiteralPath $markerPath -PathType Leaf) {
        try {
            $marker = Get-Content -LiteralPath $markerPath -Raw | ConvertFrom-Json
            $candidate = [string]$marker.version_id
            # Deliberately not a list of known values. Pinning this to the
            # protocol versions that happened to have shipped is what made the
            # resolver reject every installation from v0.8.0 on;
            # write_runtime_marker_atomic only ever rejects zero.
            $protocolVersion = [int]$marker.protocol_version
            if (-not (Test-PiInputVersionId -VersionId $candidate)) {
                throw "the marker does not carry a usable version_id."
            }
            if ($protocolVersion -lt 1) {
                throw "the marker records protocol version $protocolVersion."
            }
            $versionId = $candidate
            $source = "current.json"
        } catch {
            $failures += "current.json: $($_.Exception.Message)"
        }
    } else {
        $failures += "current.json: the marker is missing."
    }

    if ($null -eq $source) {
        try {
            $currentHostPath = [string](& $CurrentHostPathProvider)
            if ([string]::IsNullOrWhiteSpace($currentHostPath) -or
                -not [IO.Path]::IsPathRooted($currentHostPath)) {
                throw "no absolute CurrentHostPath was recorded."
            }
            if (-not (Test-PiInputSamePath -Left $currentHostPath -Right $expectedHost)) {
                throw "CurrentHostPath does not name $expectedHost."
            }
            $source = "registry"
        } catch {
            $failures += "registry: $($_.Exception.Message)"
        }
    }

    if ($null -eq $source) {
        # The Host lives at a fixed path, so an installation whose bookkeeping
        # was lost is still perfectly resolvable from what is on disk. This is
        # the same reasoning resolve_current_host applies in the installer.
        if (Test-Path -LiteralPath $expectedHost -PathType Leaf) {
            $source = "layout"
        } else {
            $failures += "layout: $expectedHost is not present."
        }
    }

    if ($null -eq $source) {
        throw "The installed PiInput runtime could not be resolved ($($failures -join ' '))."
    }

    return Resolve-PiInputRuntimeLayout -PiInputRoot $root `
        -ProgramFilesRoot $ProgramFilesRoot -VersionId $versionId `
        -Source $source -RegisteredDll $registered
}
