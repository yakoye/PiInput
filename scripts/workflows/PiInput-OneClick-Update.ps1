# 默认走覆盖升级，不先卸载。
#
# 安装器本来就是为原地升级设计的：被占用的文件改名让路，稳定的 TSF 入口保持
# 不动，只有字节真的变了才需要动 Program Files 里的 Shim。先卸载再安装等于
# 把这些工作全部推翻重做，代价有三个：
#
#   1. 卸载和安装各弹一次 UAC，用户要确认两次；
#   2. 卸载器会删掉机器级 Shim，安装器随后又装回同一个路径 —— 这正是
#      「重启后文件全部消失、只剩注册表」那次事故的成因；
#   3. 慢，而且中途失败会留下一个装不上也卸不掉的半残状态。
#
# 需要彻底重装时用 -CleanReinstall，它保留原来的先卸载再安装流程。
[CmdletBinding()]
param(
    [string]$ZipPath = "",
    [switch]$CleanReinstall,
    # 保留给既有调用方；覆盖升级现在是默认行为，这个开关不再改变任何事。
    [switch]$SkipUninstall,
    # 供自动化调用：安装器改为静默运行，不显示确认页和完成页，因此也不会打开
    # 设置程序和配置目录。无人值守时必须加，否则会停在确认页等人点。
    [switch]$NoPrompt,
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

function Invoke-VisibleProcess([string]$FilePath, [string[]]$Arguments, [string]$Label) {
    $process = Start-Process -FilePath $FilePath -ArgumentList $Arguments -PassThru -Wait
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

    if ($CleanReinstall -and (Test-Path -LiteralPath $uninstallKey)) {
        $entry = Get-ItemProperty -LiteralPath $uninstallKey
        $uninstaller = Get-UninstallExecutable ([string]$entry.QuietUninstallString)
        if (-not (Test-Path -LiteralPath $uninstaller -PathType Leaf)) {
            $uninstaller = Join-Path $env:LOCALAPPDATA "PiInput\Uninstall\PiInput-Uninstall.exe"
        }
        if (-not (Test-Path -LiteralPath $uninstaller -PathType Leaf)) {
            throw "检测到旧版安装记录，但找不到卸载器。请修复旧版卸载器后重试。"
        }

        Write-Host "正在卸载旧版（保留用户设置和词库）；这一步会单独请求一次 UAC……"
        Invoke-HiddenProcess $uninstaller @("--silent") "卸载旧版"
        $deadline = (Get-Date).AddSeconds(30)
        while ((Test-Path -LiteralPath $uninstallKey) -and (Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 250
        }
        if (Test-Path -LiteralPath $uninstallKey) {
            throw "旧版卸载完成后安装记录仍存在，已停止安装新版。"
        }
    }

    # 不提升整个脚本。安装器仅在机器级稳定 Shim 的字节确实要换时才自行请求
    # UAC，避免标准用户输入另一管理员账户凭据后把 HKCU 安装到错误账户。
    # Shim 没变的升级一次 UAC 都不需要。
    #
    # 不加 --silent。那个开关是「一个窗口都不显示」，连完成页也一并跳过，于是
    # 这个脚本得自己再实现一遍「装完打开设置和配置目录」——同一件事在 C++ 和
    # PowerShell 里各写一份，迟早会漂移。让安装器把自己的界面显示出来：确认页
    # 说明装到哪里，安装过程有进度条，完成页的勾选框负责收尾。脚本只管校验包
    # 和核对结果。
    if ($NoPrompt) {
        Write-Host "正在静默安装（覆盖升级，保留用户设置和词库）……"
        Invoke-HiddenProcess $installers[0].FullName @("--silent") "安装新版"
    } else {
        Write-Host "正在启动安装器（覆盖升级，保留用户设置和词库）；如 Windows 请求权限，请确认……"
        Invoke-VisibleProcess $installers[0].FullName @() "安装新版"
    }

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
        mode = if ($CleanReinstall) { "clean-reinstall" } else { "in-place-upgrade" }
    } | ConvertTo-Json | Set-Content -LiteralPath $logPath -Encoding UTF8

    Write-Host "更新成功：$actualBuildId" -ForegroundColor Green
    Write-Host "验证日志：$logPath" -ForegroundColor DarkGray

    # 打开设置程序和配置目录由安装器的完成页负责，那里有个默认勾选的勾选框。
    # 这里只补一句它说不到的话：默认关闭的新功能，没人提起就不会有人去开——
    # 上一版的中文模式英文候选就是这样被当成「没做」的。
    Write-Host ""
    Write-Host "部分功能默认关闭，需要在设置程序里打开，例如「中文输入时也给出英文候选」。" -ForegroundColor Cyan
}
finally {
    if (Test-Path -LiteralPath $stage) {
        Remove-Item -LiteralPath $stage -Recurse -Force
    }
}
