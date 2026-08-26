[CmdletBinding()]
param(
    [string]$ZipPath = "",
    [switch]$SkipUninstall,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Invoke-HiddenProcess([string]$FilePath, [string[]]$Arguments, [string]$Label) {
    $process = Start-Process -FilePath $FilePath -ArgumentList $Arguments -PassThru -Wait -WindowStyle Hidden
    if ($process.ExitCode -ne 0) {
        throw "$Label 失败，退出码 $($process.ExitCode)。"
    }
}

function Get-UninstallExecutable([string]$CommandLine) {
    if ([string]::IsNullOrWhiteSpace($CommandLine)) { return "" }
    $match = [regex]::Match($CommandLine, '^\s*"(?<exe>[^"]+\.exe)"')
    if (-not $match.Success) {
        $match = [regex]::Match($CommandLine, '^\s*(?<exe>.+?\.exe)(?:\s|$)')
    }
    if ($match.Success) { return $match.Groups['exe'].Value }
    return ""
}

function Get-PackageZip([string]$RequestedPath) {
    if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
        $resolved = [IO.Path]::GetFullPath($RequestedPath)
        if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
            throw "指定的 ZIP 不存在：$resolved"
        }
        return $resolved
    }

    $nearby = @(Get-ChildItem -LiteralPath $PSScriptRoot -File -Filter "PiInput-v*-windows-x64.zip")
    if ($nearby.Count -ne 1) {
        throw "脚本旁必须且只能放一个 PiInput-v*-windows-x64.zip；当前找到 $($nearby.Count) 个。"
    }
    return $nearby[0].FullName
}

function Get-ExpectedSha256([string]$HashPath) {
    if (-not (Test-Path -LiteralPath $HashPath -PathType Leaf)) {
        throw "缺少 ZIP 对应的 SHA-256 文件：$HashPath"
    }
    $token = ((Get-Content -Raw -LiteralPath $HashPath).Trim() -split '\s+')[0]
    if ($token -notmatch '^[0-9a-fA-F]{64}$') {
        throw "SHA-256 文件格式无效：$HashPath"
    }
    return $token.ToLowerInvariant()
}

$zip = Get-PackageZip $ZipPath
$hashFile = "$zip.sha256.txt"
$expectedHash = Get-ExpectedSha256 $hashFile
$actualHash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualHash -ne $expectedHash) {
    throw "ZIP 的 SHA-256 不匹配，已停止更新。"
}

$tempRoot = [IO.Path]::GetFullPath((Join-Path $env:TEMP "PiInput-OneClick-Update"))
$stage = [IO.Path]::GetFullPath((Join-Path $tempRoot ("stage-{0}-{1}" -f $PID, $actualHash.Substring(0, 12))))
$safePrefix = $tempRoot.TrimEnd('\') + '\'
if (-not $stage.StartsWith($safePrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "不安全的临时目录：$stage"
}

$uninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\PiInput"
$startedAt = Get-Date
$expectedBuildId = ""

try {
    New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null
    if (Test-Path -LiteralPath $stage) {
        Remove-Item -LiteralPath $stage -Recurse -Force
    }
    Expand-Archive -LiteralPath $zip -DestinationPath $stage

    # 发布包会在根目录和 bin 中各放一份安装器。只选择与 bin 目录同级的
    # 根安装器，不能因为递归发现运行时副本就误报或误执行。
    $installers = @(Get-ChildItem -LiteralPath $stage -Recurse -File -Filter "PiInput-Install.exe" |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.Directory.FullName "bin\PiInputHost.exe") -PathType Leaf })
    if ($installers.Count -ne 1) {
        throw "安装包内必须且只能识别出一个根 PiInput-Install.exe；当前找到 $($installers.Count) 个。"
    }
    $packageHost = Join-Path $installers[0].Directory.FullName "bin\PiInputHost.exe"
    $expectedBuildId = ((& $packageHost --build-id 2>&1) | Select-Object -First 1).ToString().Trim()
    if ([string]::IsNullOrWhiteSpace($expectedBuildId)) {
        throw "无法读取安装包的 build ID。"
    }

    Write-Host "PiInput 一键更新" -ForegroundColor Cyan
    Write-Host "安装包：$zip"
    Write-Host "SHA-256：$actualHash"
    Write-Host "目标 build ID：$expectedBuildId"

    if ($DryRun) {
        Write-Host "DryRun 通过：包结构、哈希和 build ID 均有效；没有卸载或安装。" -ForegroundColor Yellow
        return
    }

    if (-not $SkipUninstall -and (Test-Path -LiteralPath $uninstallKey)) {
        $entry = Get-ItemProperty -LiteralPath $uninstallKey
        $uninstaller = Get-UninstallExecutable ([string]$entry.QuietUninstallString)
        if (-not (Test-Path -LiteralPath $uninstaller -PathType Leaf)) {
            $uninstaller = Join-Path $env:LOCALAPPDATA "PiInput\Uninstall\PiInput-Uninstall.exe"
        }
        if (-not (Test-Path -LiteralPath $uninstaller -PathType Leaf)) {
            throw "检测到旧版安装记录，但找不到卸载器。请修复旧版卸载器后重试。"
        }

        Write-Host "正在卸载旧版（保留用户设置和词库）……"
        Invoke-HiddenProcess $uninstaller @("--silent") "卸载旧版"
        $deadline = (Get-Date).AddSeconds(30)
        while ((Test-Path -LiteralPath $uninstallKey) -and (Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 250
        }
        if (Test-Path -LiteralPath $uninstallKey) {
            throw "旧版卸载完成后安装记录仍存在，已停止安装新版。"
        }
    }

    # 不提升整个脚本。安装器仅在注册机器级稳定 Shim 时自行请求 UAC，
    # 避免标准用户输入另一管理员账户凭据后把 HKCU 安装到错误账户。
    Write-Host "正在安装新版；如 Windows 请求权限，请确认安装器的 UAC 提示……"
    Invoke-HiddenProcess $installers[0].FullName @("--silent") "安装新版"

    if (-not (Test-Path -LiteralPath $uninstallKey)) {
        throw "安装完成后没有生成 PiInput 安装记录。"
    }
    $installed = Get-ItemProperty -LiteralPath $uninstallKey
    $installedRoot = [IO.Path]::GetFullPath([string]$installed.InstallLocation)
    $installedHost = Join-Path $installedRoot "bin\PiInputHost.exe"
    if (-not (Test-Path -LiteralPath $installedHost -PathType Leaf)) {
        throw "安装后的 Host 不存在：$installedHost"
    }
    $actualBuildId = ((& $installedHost --build-id 2>&1) | Select-Object -First 1).ToString().Trim()
    if ($actualBuildId -ne $expectedBuildId) {
        throw "安装后的 build ID 不匹配：expected=$expectedBuildId actual=$actualBuildId"
    }

    $logRoot = Join-Path $env:LOCALAPPDATA "PiInput\UpdateLogs"
    New-Item -ItemType Directory -Path $logRoot -Force | Out-Null
    $logPath = Join-Path $logRoot ("update-{0}.json" -f (Get-Date -Format "yyyyMMdd-HHmmss"))
    [ordered]@{
        status = "passed"
        started_at = $startedAt.ToString("o")
        completed_at = (Get-Date).ToString("o")
        zip = $zip
        sha256 = $actualHash
        build_id = $actualBuildId
        install_location = $installedRoot
        user_data_preserved = $true
    } | ConvertTo-Json | Set-Content -LiteralPath $logPath -Encoding UTF8

    Write-Host "更新成功：$actualBuildId" -ForegroundColor Green
    Write-Host "验证日志：$logPath" -ForegroundColor DarkGray
}
finally {
    if (Test-Path -LiteralPath $stage) {
        Remove-Item -LiteralPath $stage -Recurse -Force
    }
}
