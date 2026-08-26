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
