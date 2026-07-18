param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [string]$TestDataDir = "",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$Root = $PSScriptRoot
$BuildDir = Join-Path $Root "build/windows-x64"
$InstallDir = Join-Path $Root "dist/windows-x64"

function Invoke-NativeChecked {
    param([string]$FilePath, [string[]]$ArgumentList)
    Write-Host "> $FilePath $($ArgumentList -join ' ')" -ForegroundColor DarkGray
    & $FilePath @ArgumentList
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE`: $FilePath"
    }
}

function Get-VisualStudioInstallPaths {
    $paths = [System.Collections.Generic.List[string]]::new()
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio/Installer/vswhere.exe"
    if (Test-Path $vswhere) {
        $json = & $vswhere -all -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json
        foreach ($item in $json) {
            if ($item.installationPath) { $paths.Add($item.installationPath) }
        }
    }
    foreach ($version in @("18", "17")) {
        foreach ($base in @(${env:ProgramFiles(x86)}, $env:ProgramFiles)) {
            if ([string]::IsNullOrWhiteSpace($base)) { continue }
            foreach ($edition in @("BuildTools", "Community", "Professional", "Enterprise")) {
                $candidate = Join-Path $base "Microsoft Visual Studio/$version/$edition"
                if ((Test-Path $candidate) -and -not $paths.Contains($candidate)) { $paths.Add($candidate) }
            }
        }
    }
    return $paths
}

function Find-CMakeExecutable {
    $command = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    foreach ($vsPath in Get-VisualStudioInstallPaths) {
        $candidate = Join-Path $vsPath "Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
        if (Test-Path $candidate) { return $candidate }
    }
    throw @"
CMake was not found. Install the Visual Studio components:
  - Desktop development with C++
  - C++ CMake tools for Windows
  - MSVC x64/x86 build tools
  - Windows 10 or Windows 11 SDK
"@
}

function Select-Generator([string]$CMakeExe) {
    $help = (& $CMakeExe --help 2>&1 | Out-String)
    foreach ($generator in @("Visual Studio 18 2026", "Visual Studio 17 2022")) {
        if ($help.Contains($generator)) { return $generator }
    }
    throw "No supported Visual Studio CMake generator was found."
}

function Find-CTestExecutable([string]$CMakeExe) {
    $candidate = Join-Path (Split-Path -Parent $CMakeExe) "ctest.exe"
    if (Test-Path $candidate) { return $candidate }
    $command = Get-Command ctest.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    throw "ctest.exe was not found."
}

try {
    $CMakeExe = Find-CMakeExecutable
    $CTestExe = Find-CTestExecutable $CMakeExe
    $Generator = Select-Generator $CMakeExe

    if ([string]::IsNullOrWhiteSpace($TestDataDir)) {
        $SiblingDicts = Join-Path (Split-Path -Parent $Root) "dicts"
        if (Test-Path $SiblingDicts) { $TestDataDir = $SiblingDicts }
    }

    Write-Host "LiteIME root: $Root" -ForegroundColor Cyan
    Write-Host "Using CMake: $CMakeExe" -ForegroundColor Cyan
    Write-Host "Using generator: $Generator" -ForegroundColor Cyan
    if ($TestDataDir) { Write-Host "SCEL test data: $TestDataDir" -ForegroundColor Cyan }

    if ($Clean) {
        if (Test-Path $BuildDir) { Remove-Item $BuildDir -Recurse -Force }
        if (Test-Path $InstallDir) { Remove-Item $InstallDir -Recurse -Force }
    }

    $cache = Join-Path $BuildDir "CMakeCache.txt"
    if (Test-Path $cache) {
        $cached = Get-Content $cache | Where-Object { $_ -like "CMAKE_GENERATOR:INTERNAL=*" } | Select-Object -First 1
        if ($cached -and $cached.Substring("CMAKE_GENERATOR:INTERNAL=".Length) -ne $Generator) {
            Remove-Item $BuildDir -Recurse -Force
        }
    }

    $configure = @("-S", $Root, "-B", $BuildDir, "-G", $Generator, "-A", "x64", "-DLITEIME_BUILD_TESTS=ON")
    if ($TestDataDir) { $configure += "-DLITEIME_TESTDATA_DIR=$TestDataDir" }

    Invoke-NativeChecked $CMakeExe $configure
    Invoke-NativeChecked $CMakeExe @("--build", $BuildDir, "--config", $Configuration, "--parallel")
    Invoke-NativeChecked $CTestExe @("--test-dir", $BuildDir, "-C", $Configuration, "--output-on-failure")

    if (Test-Path $InstallDir) { Remove-Item $InstallDir -Recurse -Force }
    Invoke-NativeChecked $CMakeExe @("--install", $BuildDir, "--config", $Configuration, "--prefix", $InstallDir)

    $expected = @("liteime-cli.exe", "liteime-scel-converter.exe", "liteime-lexicon-compiler.exe", "liteime-dictionary-builder.exe", "liteime-benchmark.exe", "liteime-preview.exe", "liteime-profile.exe", "LiteImeTSF.dll")
    foreach ($name in $expected) {
        $path = Join-Path $InstallDir "bin/$name"
        if (-not (Test-Path $path)) { throw "Expected executable was not generated: $path" }
    }

    Write-Host "Build, tests, and staging completed." -ForegroundColor Green
    Get-ChildItem (Join-Path $InstallDir "bin") | Where-Object { $_.Extension -in @(".exe", ".dll") } | Select-Object Name, Length, LastWriteTime
    exit 0
} catch {
    Write-Error $_
    exit 1
}
