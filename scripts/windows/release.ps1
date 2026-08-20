param(
    [string]$Version = "",
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [ValidateSet("full", "no-tests", "package-only")]
    [string]$Mode = "",
    [switch]$NoArchive,
    [switch]$NonInteractive
)

# PiInput Windows 发布入口。一次跑完：确认版本号、同步版本号、重新编译、
# 全量回归、刷新 dist、打包、生成 SHA-256、归档旧包。
#
# 版本号是这套流程唯一必须人工确认的输入。CMakeLists.txt 的 project 版本会
# 编进 piinput_core 的 PIINPUT_VERSION，安装器、Host 和测试台都从那里取值，
# 所以版本号一旦变化就必须重新编译，否则包名和程序内部会对不上。

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$VersionFile = Join-Path $Root "VERSION"
$CMakeLists = Join-Path $Root "CMakeLists.txt"
$BuildScript = Join-Path $Root "build.ps1"
$PackageScript = Join-Path $PSScriptRoot "package-release.ps1"
$ManifestScript = Join-Path $PSScriptRoot "update-source-manifest.ps1"
$Artifacts = Join-Path $Root "artifacts"
$OldPackages = Join-Path $Artifacts "old_packages"
$Utf8NoBom = [System.Text.UTF8Encoding]::new($false)

function Write-Step([string]$Text) {
    Write-Host ""
    Write-Host "== $Text" -ForegroundColor Cyan
}

function Write-Note([string]$Text) { Write-Host "   $Text" -ForegroundColor DarkGray }
function Write-Good([string]$Text) { Write-Host "   $Text" -ForegroundColor Green }
function Write-Warn([string]$Text) { Write-Host "   $Text" -ForegroundColor Yellow }

function Get-VersionFileValue {
    return ([System.IO.File]::ReadAllText($VersionFile, $Utf8NoBom)).Trim()
}

function Set-VersionFileValue([string]$NewVersion) {
    [System.IO.File]::WriteAllText($VersionFile, "$NewVersion`n", $Utf8NoBom)
}

function Get-CMakeProjectVersion {
    $text = [System.IO.File]::ReadAllText($CMakeLists, $Utf8NoBom)
    $match = [regex]::Match($text, '(?m)^project\(PiInput VERSION (?<version>[^\s)]+) LANGUAGES CXX\)')
    if (-not $match.Success) {
        throw "CMakeLists.txt 里找不到 project(PiInput VERSION ... LANGUAGES CXX) 行，无法同步版本号。"
    }
    return $match.Groups["version"].Value
}

function Set-CMakeProjectVersion([string]$NewVersion) {
    $text = [System.IO.File]::ReadAllText($CMakeLists, $Utf8NoBom)
    $updated = [regex]::Replace(
        $text,
        '(?m)^(project\(PiInput VERSION )[^\s)]+( LANGUAGES CXX\))',
        "`${1}$NewVersion`${2}")
    if ($updated -eq $text) { throw "CMakeLists.txt 版本号替换没有生效。" }
    [System.IO.File]::WriteAllText($CMakeLists, $updated, $Utf8NoBom)
}

# 已编译二进制里的版本来自 PIINPUT_VERSION，和包名对不上就说明这次没重编。
function Get-BuiltVersion {
    $hostExe = Join-Path $Root "build/windows-x64/$Configuration/PiInputHost.exe"
    if (-not (Test-Path -LiteralPath $hostExe -PathType Leaf)) { return "" }
    $output = & $hostExe --build-id 2>&1
    if ($LASTEXITCODE -ne 0) { return "" }
    return (($output | Select-Object -First 1) -as [string]).Trim()
}

function Get-VersionedDocuments([string]$Ver) {
    $base = $Ver -replace '-dev$', ''
    return [ordered]@{
        "版本说明" = Join-Path $Root "docs/release_notes_v$Ver.md"
        "验证记录" = Join-Path $Root "docs/VERIFICATION_v$Ver.md"
        "安装使用与测试" = Join-Path $Root "docs/v${base}安装、使用与测试.md"
        "长文本打字测试" = Join-Path $Root "docs/三个长文本打字测试_v$Ver.md"
    }
}

function Read-Choice([string]$Prompt, [string[]]$Choices, [int]$DefaultIndex) {
    for ($i = 0; $i -lt $Choices.Count; $i++) {
        $mark = if ($i -eq $DefaultIndex) { "(默认)" } else { "      " }
        Write-Host ("   {0}) {1} {2}" -f ($i + 1), $Choices[$i], $mark)
    }
    while ($true) {
        $answer = (Read-Host $Prompt).Trim()
        if ($answer -eq "") { return $DefaultIndex }
        $number = 0
        if ([int]::TryParse($answer, [ref]$number) -and $number -ge 1 -and $number -le $Choices.Count) {
            return $number - 1
        }
        Write-Warn "请输入 1 到 $($Choices.Count) 之间的数字，或直接回车用默认项。"
    }
}

function Invoke-Checked([string]$FilePath, [string[]]$ArgumentList) {
    Write-Note "> $FilePath $($ArgumentList -join ' ')"
    & $FilePath @ArgumentList
    if ($LASTEXITCODE -ne 0) { throw "命令失败，退出码 $LASTEXITCODE`: $FilePath" }
}

# 旧的 zip 和 sha256 留在 artifacts 根目录会让人分不清哪个是当前发布。
function Move-OldPackages([string]$Ver) {
    $keepPrefix = "PiInput-v$Ver-windows-x64"
    $stale = @(Get-ChildItem -LiteralPath $Artifacts -File |
        Where-Object { $_.Name -like "PiInput-*.zip" -or $_.Name -like "PiInput-*.zip.sha256.txt" } |
        Where-Object { -not $_.Name.StartsWith($keepPrefix) })
    if ($stale.Count -eq 0) {
        Write-Note "artifacts 根目录没有需要归档的旧包。"
        return
    }
    New-Item -ItemType Directory -Force $OldPackages | Out-Null
    foreach ($file in $stale) {
        $target = Join-Path $OldPackages $file.Name
        if (Test-Path -LiteralPath $target) { [System.IO.File]::Delete($target) }
        Move-Item -LiteralPath $file.FullName -Destination $target -Force
        Write-Note "归档 $($file.Name)"
    }
    Write-Good "已归档 $($stale.Count) 个旧包文件到 artifacts/old_packages。"
}

try {
    Write-Host "PiInput 发布工具" -ForegroundColor Cyan
    Write-Host "仓库根目录: $Root" -ForegroundColor DarkGray

    $currentVersion = Get-VersionFileValue
    $cmakeVersion = Get-CMakeProjectVersion

    Write-Step "第 1 步 / 共 7 步：确认版本号"
    Write-Note "VERSION 文件: $currentVersion"
    Write-Note "CMakeLists.txt: $cmakeVersion"
    if ($Version -eq "") {
        if ($NonInteractive) {
            $Version = $currentVersion
        } else {
            $answer = (Read-Host "请输入要发布的版本号（直接回车用 $currentVersion）").Trim()
            $Version = if ($answer -eq "") { $currentVersion } else { $answer }
        }
    }
    $Version = $Version -replace '^v', ''
    if ($Version -notmatch '^\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$') {
        throw "版本号格式不对: $Version（应为 1.2.3 或 1.2.3-dev）"
    }
    $versionChanged = ($Version -ne $currentVersion) -or ($Version -ne $cmakeVersion)
    Write-Good "本次发布版本: v$Version"
    if ($versionChanged) { Write-Warn "版本号有变化，必须重新编译，否则程序内部仍报旧版本。" }

    Write-Step "第 2 步 / 共 7 步：检查随包文档"
    # 文档缺失在编译前就要暴露，不能让人白等一次全量编译。
    $documents = Get-VersionedDocuments $Version
    $missing = @()
    foreach ($name in $documents.Keys) {
        $path = $documents[$name]
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            Write-Note "$name  $(Split-Path -Leaf $path)"
        } else {
            $missing += "$name  $(Split-Path -Leaf $path)"
        }
    }
    if ($missing.Count -gt 0) {
        throw ("v$Version 缺少随包文档，请先补齐 docs/ 下的这些文件：`n    " + ($missing -join "`n    "))
    }
    Write-Good "随包文档齐全。"

    Write-Step "第 3 步 / 共 7 步：选择编译方式"
    if ($Mode -eq "") {
        if ($NonInteractive) {
            $Mode = "full"
        } else {
            $choices = @(
                "重新编译 + 全量 CTest 回归 + 打包",
                "重新编译 + 跳过测试 + 打包",
                "只打包，直接用当前 build 目录的产物")
            $Mode = @("full", "no-tests", "package-only")[(Read-Choice "请选择" $choices 0)]
        }
    }
    if ($versionChanged -and $Mode -eq "package-only") {
        throw "版本号从 $cmakeVersion 变成 $Version，不能只打包。请选重新编译。"
    }
    Write-Note "编译方式: $Mode"

    Write-Step "第 4 步 / 共 7 步：同步版本号与源码清单"
    if ($versionChanged) {
        Set-VersionFileValue $Version
        Set-CMakeProjectVersion $Version
        Write-Good "已把 VERSION 和 CMakeLists.txt 同步到 $Version。"
    } else {
        Write-Note "版本号无变化。"
    }
    # 必须排在编译前面：改过 VERSION 和 CMakeLists.txt 之后，
    # CTest 的 piinput-sha256-regression 会拿旧哈希去比对新文件。
    Invoke-Checked "pwsh" @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $ManifestScript)

    Write-Step "第 5 步 / 共 7 步：编译与回归"
    if ($Mode -eq "package-only") {
        Write-Warn "按要求跳过编译。"
    } else {
        $buildArgs = @("-Configuration", $Configuration)
        if ($Mode -eq "no-tests") { $buildArgs += "-SkipTests" }
        Invoke-Checked "pwsh" (@("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $BuildScript) + $buildArgs)
        Write-Good "编译完成，dist/windows-x64 已刷新。"
    }

    # 包名写着 v0.7.1、程序里报 0.7.0 这种事只能靠机器卡住。
    $builtVersion = Get-BuiltVersion
    if ($builtVersion -eq "") {
        throw "读不出已编译二进制的版本号，请确认 build/windows-x64/$Configuration/PiInputHost.exe 存在。"
    }
    if ($builtVersion -ne $Version) {
        throw "已编译二进制内嵌版本是 $builtVersion，和要发布的 $Version 不一致。请重新编译后再发布。"
    }
    Write-Good "二进制内嵌版本校验通过: $builtVersion"

    Write-Step "第 6 步 / 共 7 步：打包"
    Invoke-Checked "pwsh" @(
        "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $PackageScript,
        "-Configuration", $Configuration, "-Version", $Version)

    Write-Step "第 7 步 / 共 7 步：归档旧包"
    if ($NoArchive) {
        Write-Note "按要求跳过归档。"
    } else {
        Move-OldPackages $Version
    }

    $packageName = "PiInput-v$Version-windows-x64"
    $packageRoot = Join-Path $Artifacts $packageName
    $zip = Join-Path $Artifacts "$packageName.zip"
    $hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    $sizeMb = [math]::Round((Get-Item -LiteralPath $zip).Length / 1MB, 2)
    $fileCount = @(Get-ChildItem -LiteralPath $packageRoot -File -Recurse).Count

    Write-Host ""
    Write-Host "发布完成: v$Version" -ForegroundColor Green
    Write-Host "  目录    $packageRoot" -ForegroundColor Green
    Write-Host "  压缩包  $zip" -ForegroundColor Green
    Write-Host "  大小    $sizeMb MB，共 $fileCount 个文件" -ForegroundColor Green
    Write-Host "  SHA-256 $hash" -ForegroundColor Green
    exit 0
} catch {
    Write-Host ""
    Write-Host "发布中断: $_" -ForegroundColor Red
    exit 1
}
