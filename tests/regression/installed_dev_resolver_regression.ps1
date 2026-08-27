$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
. (Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) "scripts/windows/resolve-installed-dev.ps1")

# The fixture models the layout PiInput actually installs: one bin directory
# under the product root, a marker beside it that names a build rather than a
# directory, and the registered Shim held at a fixed machine-wide path. The
# previous fixture built a per-version tree under Runtime\versions, which is why
# it kept passing long after no installation looked like that any more.
$root = Join-Path ([IO.Path]::GetTempPath()) ("piinput-runtime-resolver-" + [guid]::NewGuid())

function Assert-ResolverThrows {
    param(
        [Parameter(Mandatory = $true)][scriptblock]$Action,
        [Parameter(Mandatory = $true)][string]$Pattern,
        [Parameter(Mandatory = $true)][string]$Label
    )
    try {
        & $Action | Out-Null
    } catch {
        if ($_.Exception.Message -match $Pattern) { return }
        throw "$Label threw the wrong error: $($_.Exception.Message)"
    }
    throw "$Label did not fail"
}

try {
    $local = Join-Path $root "Local"
    $programFiles = Join-Path $root "ProgramFiles"
    $product = Join-Path $local "PiInput"
    $bin = Join-Path $product "bin"
    $marker = Join-Path $product "current.json"
    $machineShim = Join-Path $programFiles "PiInput/Runtime/Shim/PiInputTSF.dll"
    $expectedHost = Join-Path $bin "PiInputHost.exe"
    $version = "0.8.0-20260826-142401-10020"

    New-Item -ItemType Directory -Force $bin, (Split-Path -Parent $machineShim) | Out-Null
    foreach ($name in @(
        "PiInputHost.exe", "piinput-profile.exe", "piinput-cli.exe", "PiInputTSF.dll"
    )) {
        Set-Content -LiteralPath (Join-Path $bin $name) -Value "fixture" -Encoding ASCII
    }
    Set-Content -LiteralPath $machineShim -Value "shim" -Encoding ASCII
    Set-Content -LiteralPath $marker -Encoding UTF8 `
        -Value (@{ version_id = $version; protocol_version = 5 } | ConvertTo-Json)

    if (Test-Path -LiteralPath (Join-Path $product "Runtime")) {
        throw "the fixture must not contain a per-version runtime tree"
    }

    $layout = Resolve-PiInputInstalledDev -LocalAppDataRoot $local -ProgramFilesRoot $programFiles `
        -RegisteredDllPathProvider { $machineShim } -CurrentHostPathProvider { $expectedHost }
    if ($layout.Source -ne "current.json" -or $layout.VersionId -ne $version) {
        throw "current.json did not identify the installed build"
    }
    if ($layout.Bin -ne $bin -or $layout.Host -ne $expectedHost -or
        $layout.Profile -ne (Join-Path $bin "piinput-profile.exe") -or
        $layout.Cli -ne (Join-Path $bin "piinput-cli.exe") -or
        $layout.DeveloperRoot -ne $bin -or $layout.Data -ne (Join-Path $product "data")) {
        throw "the resolver did not resolve the unified bin directory"
    }
    if ($layout.Dll -ne ([IO.Path]::GetFullPath($machineShim)) -or
        $layout.UserShim -ne (Join-Path $bin "PiInputTSF.dll")) {
        throw "the resolver did not separate the registered Shim from the per-user copy"
    }
    foreach ($abandoned in @("VersionsRoot", "ActiveVersionRoot")) {
        if ($layout.PSObject.Properties.Name -contains $abandoned) {
            throw "the resolver still advertises the abandoned per-version layout: $abandoned"
        }
    }

    # A protocol far beyond anything that has shipped. Rejecting an unfamiliar
    # protocol is what broke this resolver against every v0.8.0 installation,
    # and the installer only ever refuses zero.
    Set-Content -LiteralPath $marker -Encoding UTF8 `
        -Value (@{ version_id = $version; protocol_version = 99 } | ConvertTo-Json)
    if ((Resolve-PiInputInstalledDev -LocalAppDataRoot $local -ProgramFilesRoot $programFiles `
            -RegisteredDllPathProvider { $machineShim } `
            -CurrentHostPathProvider { $expectedHost }).Source -ne "current.json") {
        throw "the resolver refused a marker whose protocol it had not seen before"
    }

    # Every unusable marker costs the version string and nothing else: the Host
    # sits at a fixed path, so the registry still confirms the installation.
    foreach ($rejected in @(
        '{broken',
        (@{ version_id = $version; protocol_version = 0 } | ConvertTo-Json),
        (@{ version_id = "..\outside"; protocol_version = 5 } | ConvertTo-Json)
    )) {
        Set-Content -LiteralPath $marker -Value $rejected -Encoding UTF8
        $fallback = Resolve-PiInputInstalledDev -LocalAppDataRoot $local `
            -ProgramFilesRoot $programFiles -RegisteredDllPathProvider { $machineShim } `
            -CurrentHostPathProvider { $expectedHost }
        if ($fallback.Source -ne "registry" -or $fallback.Bin -ne $bin -or
            $fallback.VersionId -ne "") {
            throw "an unusable marker did not fall through to the registry: $rejected"
        }
    }

    # CurrentHostPath is a hint, not the definition. Neither its absence nor a
    # stale value pointing outside the installation may stop the resolver, and
    # neither may make it resolve anything other than the fixed layout.
    $outside = Join-Path $root "outside/PiInputHost.exe"
    New-Item -ItemType Directory -Force (Split-Path -Parent $outside) | Out-Null
    Set-Content -LiteralPath $outside -Value "fixture" -Encoding ASCII
    foreach ($provider in @({ $null }, { $outside }, { "PiInputHost.exe" })) {
        $onDisk = Resolve-PiInputInstalledDev -LocalAppDataRoot $local `
            -ProgramFilesRoot $programFiles -RegisteredDllPathProvider { $machineShim } `
            -CurrentHostPathProvider $provider
        if ($onDisk.Source -ne "layout" -or $onDisk.Host -ne $expectedHost) {
            throw "the resolver did not fall back to the layout on disk"
        }
    }

    Assert-ResolverThrows -Label "a registration outside the machine Shim" `
        -Pattern "machine-wide stable Shim" -Action {
        Resolve-PiInputInstalledDev -LocalAppDataRoot $local -ProgramFilesRoot $programFiles `
            -RegisteredDllPathProvider { $outside } -CurrentHostPathProvider { $expectedHost }
    }

    $missingHost = "$expectedHost.absent"
    Rename-Item -LiteralPath $expectedHost -NewName (Split-Path -Leaf $missingHost)
    Assert-ResolverThrows -Label "an installation missing its Host" `
        -Pattern "runtime file is missing" -Action {
        Resolve-PiInputInstalledDev -LocalAppDataRoot $local -ProgramFilesRoot $programFiles `
            -RegisteredDllPathProvider { $machineShim } -CurrentHostPathProvider { $expectedHost }
    }
    Rename-Item -LiteralPath $missingHost -NewName (Split-Path -Leaf $expectedHost)

    Remove-Item -LiteralPath $machineShim -Force
    Assert-ResolverThrows -Label "an installation without machine COM activation" `
        -Pattern "PiInput-Install\.exe" -Action {
        Resolve-PiInputInstalledDev -LocalAppDataRoot $local -ProgramFilesRoot $programFiles `
            -RegisteredDllPathProvider { "" } -CurrentHostPathProvider { $expectedHost }
    }
}
finally {
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "Installed stable-runtime resolver regression passed."
