param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [string]$TestDataDir = "",
    [string]$TestReportPath = "",
    [switch]$Clean,
    [switch]$SkipTests
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$Root = $PSScriptRoot
$BuildDir = Join-Path $Root "build/windows-x64"
$InstallDir = Join-Path $Root "dist/windows-x64"
# 32 位只出一个文件：TSF Shim。它是薄客户端——不链 piinput_core，词库和引擎都
# 在 Host 里——所以 32 位程序里的输入法连的仍是同一个 64 位 Host，词库只有一份，
# 学习记录也共用。
#
# 不出这一份，32 位程序里就根本没有 PiInput 可选：32 位进程只能加载 32 位 DLL，
# 也只看得见 WOW6432Node 那个注册表视图。MobaXterm 就是这样，切过去等于没装。
$X86BuildDir = Join-Path $Root "build/windows-x86"

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

    Write-Host "PiInput root: $Root" -ForegroundColor Cyan
    Write-Host "Using CMake: $CMakeExe" -ForegroundColor Cyan
    Write-Host "Using generator: $Generator" -ForegroundColor Cyan
    if ($TestDataDir) { Write-Host "SCEL test data: $TestDataDir" -ForegroundColor Cyan }

    if ($Clean) {
        foreach ($stale in @($BuildDir, $InstallDir, $X86BuildDir)) {
            if (Test-Path $stale) { Remove-Item $stale -Recurse -Force }
        }
    }

    $cache = Join-Path $BuildDir "CMakeCache.txt"
    if (Test-Path $cache) {
        $cached = Get-Content $cache | Where-Object { $_ -like "CMAKE_GENERATOR:INTERNAL=*" } | Select-Object -First 1
        if ($cached -and $cached.Substring("CMAKE_GENERATOR:INTERNAL=".Length) -ne $Generator) {
            Remove-Item $BuildDir -Recurse -Force
        }
    }

    $configure = @("-S", $Root, "-B", $BuildDir, "-G", $Generator, "-A", "x64", "-DPIINPUT_BUILD_TESTS=ON")
    if ($TestDataDir) { $configure += "-DPIINPUT_TESTDATA_DIR=$TestDataDir" }

    Invoke-NativeChecked $CMakeExe $configure
    Invoke-NativeChecked $CMakeExe @("--build", $BuildDir, "--config", $Configuration, "--parallel")
    if (-not $SkipTests) {
        $ctestArguments = @("--test-dir", $BuildDir, "-C", $Configuration, "--output-on-failure")
        if (-not [string]::IsNullOrWhiteSpace($TestReportPath)) {
            $resolvedTestReport = [IO.Path]::GetFullPath($TestReportPath)
            $testReportParent = Split-Path -Parent $resolvedTestReport
            if (-not [string]::IsNullOrWhiteSpace($testReportParent)) {
                New-Item -ItemType Directory -Force -Path $testReportParent | Out-Null
            }
            $ctestArguments += @("--output-junit", $resolvedTestReport)
        }
        Invoke-NativeChecked $CTestExe $ctestArguments
    }

    # 32 位那一趟。单独的构建目录，因为一次 CMake 配置只对应一个目标平台。
    # 只构建 PiInputTSF，不跑测试：这份产物和 64 位那份是同一批源码，逻辑测试
    # 在 64 位那趟已经跑过，这里要验证的是它能不能编成 32 位、导出对不对。
    $x86Configure = @(
        "-S", $Root, "-B", $X86BuildDir, "-G", $Generator, "-A", "Win32",
        "-DPIINPUT_BUILD_TESTS=OFF")
    Invoke-NativeChecked $CMakeExe $x86Configure
    Invoke-NativeChecked $CMakeExe @(
        "--build", $X86BuildDir, "--config", $Configuration, "--target", "PiInputTSF", "--parallel")
    $x86Dll = Join-Path $X86BuildDir "$Configuration/PiInputTSF.dll"
    if (-not (Test-Path $x86Dll)) { throw "32 位 TSF DLL 未生成：$x86Dll" }

    # PE 头里的 machine 字段。搞混两个位数不会有编译错误，只会在安装后表现为
    # 「切过去输入法是灰的」，那时候再查代价高得多。
    $x86Bytes = [System.IO.File]::ReadAllBytes($x86Dll)
    $x86Machine = [BitConverter]::ToUInt16(
        $x86Bytes, [BitConverter]::ToInt32($x86Bytes, 0x3C) + 4)
    if ($x86Machine -ne 0x014C) {
        throw ("32 位 TSF DLL 的 PE machine 是 0x{0:X4}，应为 0x014C" -f $x86Machine)
    }

    if (Test-Path $InstallDir) { Remove-Item $InstallDir -Recurse -Force }
    Invoke-NativeChecked $CMakeExe @("--install", $BuildDir, "--config", $Configuration, "--prefix", $InstallDir)

    # 放进 bin/x86/，因为打包是把整个 bin/ 复制过去的——搁在别处就得同时改打包
    # 脚本和包内清单，而那两处漏掉一个的后果是包里没有 32 位 shim，安装后
    # 32 位程序照样用不了，且不会有任何报错。
    $x86Target = Join-Path $InstallDir "bin/x86"
    New-Item -ItemType Directory -Force -Path $x86Target | Out-Null
    Copy-Item $x86Dll (Join-Path $x86Target "PiInputTSF.dll") -Force
    Write-Host "32-bit TSF shim staged: $x86Target/PiInputTSF.dll" -ForegroundColor Green

    $expected = @("piinput-cli.exe", "piinput-scel-converter.exe", "piinput-lexicon-compiler.exe", "piinput-dictionary-builder.exe", "piinput-benchmark.exe", "piinput-preview.exe", "piinput-profile.exe", "piinput-diagnostics.exe", "PiInputHost.exe", "PiInput-Settings.exe", "PiInputTSF.dll", "PiInput-Install.exe", "PiInput-Uninstall.exe")
    foreach ($name in $expected) {
        $path = Join-Path $InstallDir "bin/$name"
        if (-not (Test-Path $path)) { throw "Expected executable was not generated: $path" }
    }

    $legacyCompact = "lite" + "ime"
    $legacyHyphen = "lite" + "-" + "ime"
    $legacyUnderscore = "lite" + "_" + "ime"
    $legacyPattern = "$legacyCompact|$legacyHyphen|$legacyUnderscore"
    $legacyArtifacts = @(Get-ChildItem (Join-Path $InstallDir "bin") -File |
        Where-Object { $_.Name -match $legacyPattern })
    if ($legacyArtifacts.Count -gt 0) {
        throw "Legacy Release artifact remains: $($legacyArtifacts.Name -join ', ')"
    }

    $summary = if ($SkipTests) { "Build and staging completed (tests intentionally deferred)." } else { "Build, tests, and staging completed." }
    Write-Host $summary -ForegroundColor Green
    Get-ChildItem (Join-Path $InstallDir "bin") | Where-Object { $_.Extension -in @(".exe", ".dll") } | Select-Object Name, Length, LastWriteTime
    exit 0
} catch {
    Write-Error $_
    exit 1
}
