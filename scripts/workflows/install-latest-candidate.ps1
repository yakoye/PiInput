[CmdletBinding()]
param(
    [string]$CandidatesRoot = "",
    # 覆盖升级是默认路径；只有需要把旧版彻底清掉时才加 -CleanReinstall，
    # 它会多要一次 UAC。
    [switch]$CleanReinstall,
    [switch]$SkipUninstall,
    # 供自动化调用：成功后不询问是否打开设置程序。
    [switch]$NoPrompt,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$WorkspaceRoot = Split-Path -Parent $RepoRoot
if ([string]::IsNullOrWhiteSpace($CandidatesRoot)) {
    $CandidatesRoot = Join-Path $WorkspaceRoot "releases\candidates"
}
$CandidatesRoot = [IO.Path]::GetFullPath($CandidatesRoot)
$Updater = Join-Path $PSScriptRoot "PiInput-OneClick-Update.ps1"

function Get-CandidateInfo([IO.DirectoryInfo]$Directory) {
    if ($Directory.Name -notmatch '^v(?<version>\d+\.\d+\.\d+(?:-[0-9A-Za-z.]+)?)-(?<stamp>\d{6}_\d{4})-(?<commit>[0-9a-f]{12})$') {
        return $null
    }
    $stampText = $Matches.stamp
    $versionText = $Matches.version
    $commitText = $Matches.commit
    $timestamp = [datetime]::ParseExact(
        $stampText, "yyMMdd_HHmm", [Globalization.CultureInfo]::InvariantCulture)
    $zips = @(Get-ChildItem -LiteralPath $Directory.FullName -File -Filter "PiInput-v*-windows-x64.zip")
    if ($zips.Count -ne 1) { return $null }
    [pscustomobject]@{
        Directory = $Directory.FullName
        Name = $Directory.Name
        Version = $versionText
        Commit = $commitText
        Timestamp = $timestamp
        Zip = $zips[0].FullName
    }
}

if (-not (Test-Path -LiteralPath $Updater -PathType Leaf)) {
    throw "缺少通用一键更新脚本：$Updater"
}
$candidates = @(Get-ChildItem -LiteralPath $CandidatesRoot -Directory -ErrorAction Stop |
    ForEach-Object { Get-CandidateInfo $_ } |
    Where-Object { $null -ne $_ } |
    Sort-Object Timestamp, Name -Descending)
if ($candidates.Count -eq 0) {
    throw "没有找到符合 v版本-yyMMdd_HHmm-commit12 命名且只包含一个 ZIP 的候选。"
}

$selected = $candidates[0]
Write-Host "按目录时间选择最新候选：$($selected.Name)" -ForegroundColor Cyan
$arguments = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $Updater, "-ZipPath", $selected.Zip)
if ($CleanReinstall) { $arguments += "-CleanReinstall" }
if ($SkipUninstall) { $arguments += "-SkipUninstall" }
if ($NoPrompt) { $arguments += "-NoPrompt" }
if ($DryRun) { $arguments += "-DryRun" }
& powershell.exe @arguments
if ($LASTEXITCODE -ne 0) {
    throw "最新候选一键更新失败，退出码 $LASTEXITCODE。"
}
