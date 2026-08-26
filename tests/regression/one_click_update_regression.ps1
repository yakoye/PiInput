param(
    [Parameter(Mandatory = $true)]
    [string]$UpdaterScript,
    [Parameter(Mandatory = $true)]
    [string]$InstallerExe,
    [Parameter(Mandatory = $true)]
    [string]$HostExe
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

# 分发脚本必须自带 UTF-8 BOM。
#
# 这个更新器要发到别人的机器上双击运行，启动它的是 Windows PowerShell 5.1。
# 5.1 读没有 BOM 的文件时按本机 ANSI 代码页解码，在常规简体中文 Windows 上
# 就是 GBK：脚本里的中文注释会碎成乱码，乱码字节又被当成引号和括号，整个
# 脚本的语法结构随之崩塌，报出一串「缺少右括号」「字符串缺少终止符」。
#
# 这一条不能靠「跑一遍看报不报错」来守：开发机若打开了「使用 Unicode UTF-8
# 提供全球语言支持」，ACP 就是 65001，同一个无 BOM 文件在本机恰好读得对，
# 测试照样通过，故障只在别人机器上出现。所以直接断言文件字节。
function Assert-Utf8Bom([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 3 -or
        $bytes[0] -ne 0xEF -or $bytes[1] -ne 0xBB -or $bytes[2] -ne 0xBF) {
        throw "分发脚本缺少 UTF-8 BOM，在非 UTF-8 代码页的机器上会解析失败：$Path"
    }
}
Assert-Utf8Bom $UpdaterScript

# 与之相反，.cmd 没有编码标记可言，BOM 反而会被命令解释器当成命令的一部分。
# 它里面因此不能出现任何非 ASCII 字符——中文提示一律由 PowerShell 脚本输出。
$launcher = Join-Path (Split-Path -Parent $UpdaterScript) "一键更新PiInput.cmd"
if (Test-Path -LiteralPath $launcher) {
    $launcherBytes = [IO.File]::ReadAllBytes($launcher)
    if ($launcherBytes[0] -eq 0xEF -and $launcherBytes[1] -eq 0xBB -and $launcherBytes[2] -eq 0xBF) {
        throw "启动器 .cmd 不能带 BOM，命令解释器会把它当成命令：$launcher"
    }
    foreach ($byte in $launcherBytes) {
        if ($byte -gt 0x7F) {
            throw "启动器 .cmd 含非 ASCII 字节，换一台语言不同的 Windows 就会乱码：$launcher"
        }
    }
}

$tempRoot = Join-Path $env:TEMP ("PiInput-OneClick-Update-Test-" + [guid]::NewGuid().ToString("N"))
$packageName = "PiInput-v0.0.0-windows-x64"
$packageRoot = Join-Path $tempRoot $packageName
$binRoot = Join-Path $packageRoot "bin"
$zipPath = Join-Path $tempRoot "$packageName.zip"
$hashPath = "$zipPath.sha256.txt"

try {
    New-Item -ItemType Directory -Path $binRoot -Force | Out-Null
    Copy-Item -LiteralPath $InstallerExe -Destination (Join-Path $packageRoot "PiInput-Install.exe")
    Copy-Item -LiteralPath $InstallerExe -Destination (Join-Path $binRoot "PiInput-Install.exe")
    Copy-Item -LiteralPath $HostExe -Destination (Join-Path $binRoot "PiInputHost.exe")
    Compress-Archive -LiteralPath $packageRoot -DestinationPath $zipPath

    $hash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $([IO.Path]::GetFileName($zipPath))" | Set-Content -LiteralPath $hashPath -Encoding ASCII

    $output = (& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $UpdaterScript -ZipPath $zipPath -DryRun 2>&1 | Out-String)
    if ($LASTEXITCODE -ne 0) {
        throw "合法测试包的 DryRun 失败：$output"
    }
    if ($output -notmatch "DryRun") {
        throw "合法测试包没有输出 DryRun 成功标记。"
    }

    $stream = [IO.File]::Open($zipPath, [IO.FileMode]::Append, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try { $stream.WriteByte(0x00) } finally { $stream.Dispose() }
    $tampered = (& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $UpdaterScript -ZipPath $zipPath -DryRun 2>&1 | Out-String)
    if ($LASTEXITCODE -eq 0) {
        throw "被篡改的测试包不应通过 SHA-256 门禁。"
    }
    if ($tampered -notmatch "SHA-256") {
        throw "被篡改的测试包失败信息没有指出 SHA-256。"
    }

    Write-Host "One-click update regression passed."
}
finally {
    if (Test-Path -LiteralPath $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force
    }
}
