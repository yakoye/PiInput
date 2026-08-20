$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
. (Join-Path (Split-Path -Parent $PSScriptRoot) "scripts/windows/resolve-installed-dev.ps1")

$root = Join-Path ([IO.Path]::GetTempPath()) ("piinput-runtime-resolver-" + [guid]::NewGuid())
try {
    $product = Join-Path $root "PiInput"
    $runtime = Join-Path $product "Runtime"
    $versions = Join-Path $runtime "versions"
    $version = "0.4.0-test"
    $active = Join-Path $versions $version
    $bin = Join-Path $active "bin"
    $shim = Join-Path $runtime "Shim/PiInputTSF.dll"
    New-Item -ItemType Directory -Force $bin,(Split-Path -Parent $shim) | Out-Null
    foreach ($name in @("PiInputHost.exe", "piinput-profile.exe", "piinput-cli.exe")) {
        Set-Content -LiteralPath (Join-Path $bin $name) -Value "fixture" -Encoding ASCII
    }
    Set-Content -LiteralPath $shim -Value "shim" -Encoding ASCII
    $marker = @{ version_id = $version; protocol_version = 2 } | ConvertTo-Json
    Set-Content -LiteralPath (Join-Path $runtime "current.json") -Value $marker -Encoding UTF8

    $layout = Resolve-PiInputInstalledDev -LocalAppDataRoot $root `
        -RegisteredDllPathProvider { $shim } `
        -CurrentHostPathProvider { Join-Path $bin "PiInputHost.exe" }
    if ($layout.Source -ne "current.json" -or $layout.Dll -ne $shim -or
        $layout.Host -ne (Join-Path $bin "PiInputHost.exe") -or $layout.Bin -ne $bin) {
        throw "current.json did not resolve the stable runtime layout"
    }

    Set-Content -LiteralPath (Join-Path $runtime "current.json") -Value '{broken' -Encoding UTF8
    $fallback = Resolve-PiInputInstalledDev -LocalAppDataRoot $root `
        -RegisteredDllPathProvider { $shim } `
        -CurrentHostPathProvider { Join-Path $bin "PiInputHost.exe" }
    if ($fallback.Source -ne "registry" -or $fallback.ActiveVersionRoot -ne $active) {
        throw "registry fallback did not resolve CurrentHostPath"
    }

    $outside = Join-Path $root "outside/PiInputHost.exe"
    New-Item -ItemType Directory -Force (Split-Path -Parent $outside) | Out-Null
    Set-Content -LiteralPath $outside -Value "fixture" -Encoding ASCII
    $failed = $false
    try {
        Resolve-PiInputInstalledDev -LocalAppDataRoot $root `
            -RegisteredDllPathProvider { $shim } `
            -CurrentHostPathProvider { $outside } | Out-Null
    } catch { $failed = $_.Exception.Message -match "outside the versions directory" }
    if (-not $failed) { throw "resolver accepted an outside CurrentHostPath" }

    $failed = $false
    try {
        Resolve-PiInputInstalledDev -LocalAppDataRoot $root `
            -RegisteredDllPathProvider { $outside } `
            -CurrentHostPathProvider { Join-Path $bin "PiInputHost.exe" } | Out-Null
    } catch { $failed = $_.Exception.Message -match "permanent stable Shim" }
    if (-not $failed) { throw "resolver accepted a non-stable registered DLL" }
}
finally {
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "Installed stable-runtime resolver regression passed."
