[CmdletBinding()]
param(
    [Parameter(Position = 0, Mandatory = $true)]
    [ValidateSet("status", "build", "test", "commit", "candidate", "promote", "organize")]
    [string]$Action,
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [string]$Version = "",
    [string]$BuildId = "",
    [string]$Message = "",
    [string[]]$Paths = @(),
    [switch]$Clean,
    [switch]$GatesPassed,
    [switch]$ArchiveHistoricalWorktrees,
    [switch]$ArchiveBuildCaches
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$WorkspaceRoot = Split-Path -Parent $RepoRoot
$Releases = Join-Path $WorkspaceRoot "releases"
$BuildScript = Join-Path $RepoRoot "build.ps1"
$ReleaseScript = Join-Path $RepoRoot "scripts\windows\release.ps1"
$ManifestScript = Join-Path $RepoRoot "scripts\windows\update-source-manifest.ps1"
$OrganizeScript = Join-Path $RepoRoot "scripts\maintenance\organize-workspace.ps1"

function Invoke-Checked([string]$FilePath, [string[]]$Arguments) {
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) { throw "命令失败（$LASTEXITCODE）：$FilePath" }
}

function Assert-CleanRepository {
    $changes = @(& git -C $RepoRoot status --porcelain)
    if ($changes.Count -gt 0) {
        throw "候选构建要求 Git 工作区干净。请先提交或处理改动。"
    }
}

function Get-CurrentVersion {
    return (Get-Content -Raw -LiteralPath (Join-Path $RepoRoot "VERSION")).Trim()
}

function Test-PackageHash([string]$Directory, [string]$Ver) {
    $name = "PiInput-v$Ver-windows-x64.zip"
    $zip = Join-Path $Directory $name
    $hashFile = "$zip.sha256.txt"
    if (-not (Test-Path -LiteralPath $zip) -or -not (Test-Path -LiteralPath $hashFile)) {
        throw "候选目录缺少 ZIP 或 SHA-256：$Directory"
    }
    $expected = ((Get-Content -Raw -LiteralPath $hashFile).Trim() -split '\s+')[0].ToLowerInvariant()
    $actual = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($expected -ne $actual) { throw "候选 ZIP 的 SHA-256 不匹配。" }
}

switch ($Action) {
    "status" {
        Write-Host "源码：$RepoRoot" -ForegroundColor Cyan
        Write-Host "交付：$Releases" -ForegroundColor Cyan
        & git -C $RepoRoot status --short
        Write-Host "`n正式最新版："
        Get-ChildItem -LiteralPath (Join-Path $Releases "current") -Force -ErrorAction SilentlyContinue | Select-Object Name, LastWriteTime
        Write-Host "`n候选版："
        Get-ChildItem -LiteralPath (Join-Path $Releases "candidates") -Directory -ErrorAction SilentlyContinue | Select-Object Name, LastWriteTime
    }
    "build" {
        $args = @("-Configuration", $Configuration)
        if ($Clean) { $args += "-Clean" }
        Invoke-Checked "pwsh" (@("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $BuildScript) + $args)
    }
    "test" {
        $args = @("-Configuration", $Configuration)
        if ($Clean) { $args += "-Clean" }
        Invoke-Checked "pwsh" (@("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $BuildScript) + $args)
    }
    "commit" {
        if ([string]::IsNullOrWhiteSpace($Message)) { throw "commit 操作必须提供 -Message。" }
        Invoke-Checked "pwsh" @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $ManifestScript)
        if ($Paths.Count -gt 0) { & git -C $RepoRoot add -- @Paths } else { & git -C $RepoRoot add -A }
        if ($LASTEXITCODE -ne 0) { throw "git add 失败。" }
        & git -C $RepoRoot diff --cached --quiet
        if ($LASTEXITCODE -eq 0) { throw "没有可提交的改动。" }
        & git -C $RepoRoot commit -m $Message
        if ($LASTEXITCODE -ne 0) { throw "git commit 失败。" }
        Write-Host "已创建本地提交；未执行 push。" -ForegroundColor Green
    }
    "candidate" {
        Assert-CleanRepository
        if ([string]::IsNullOrWhiteSpace($Version)) { $Version = Get-CurrentVersion }
        Invoke-Checked "pwsh" @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $ReleaseScript, "-Version", $Version, "-Configuration", $Configuration, "-Mode", "full", "-NonInteractive")
        $commit = (& git -C $RepoRoot rev-parse --short=12 HEAD).Trim()
        $candidate = Join-Path $Releases "candidates\v$Version-$commit"
        if (Test-Path -LiteralPath $candidate) { throw "候选目录已存在：$candidate" }
        New-Item -ItemType Directory -Path $candidate -Force | Out-Null
        $packageName = "PiInput-v$Version-windows-x64"
        foreach ($name in @($packageName, "$packageName.zip", "$packageName.zip.sha256.txt")) {
            Copy-Item -LiteralPath (Join-Path $RepoRoot "artifacts\$name") -Destination (Join-Path $candidate $name) -Recurse
        }
        Test-PackageHash $candidate $Version
        Write-Host "候选包已归档：$candidate" -ForegroundColor Green
        Write-Host "它尚不是正式发布，不会自动创建 tag、GitHub Release 或推送。" -ForegroundColor Yellow
    }
    "promote" {
        if (-not $GatesPassed) { throw "只有全部发布门禁通过后才能显式提供 -GatesPassed。" }
        if ([string]::IsNullOrWhiteSpace($Version) -or [string]::IsNullOrWhiteSpace($BuildId)) {
            throw "promote 必须同时提供 -Version 和 -BuildId。"
        }
        $candidate = Join-Path $Releases "candidates\v$Version-$BuildId"
        if (-not (Test-Path -LiteralPath $candidate)) { throw "候选目录不存在：$candidate" }
        Test-PackageHash $candidate $Version
        $currentRoot = Join-Path $Releases "current"
        $historyRoot = Join-Path $Releases "history"
        New-Item -ItemType Directory -Path $currentRoot, $historyRoot -Force | Out-Null
        foreach ($old in @(Get-ChildItem -LiteralPath $currentRoot -Directory -ErrorAction SilentlyContinue)) {
            $target = Join-Path $historyRoot $old.Name
            if (Test-Path -LiteralPath $target) { $target += "-" + (Get-Date -Format "yyyyMMdd-HHmmss") }
            Move-Item -LiteralPath $old.FullName -Destination $target
        }
        Move-Item -LiteralPath $candidate -Destination (Join-Path $currentRoot "v$Version")
        Write-Host "已在本地提升为 current：v$Version" -ForegroundColor Green
        Write-Host "仍需单独完成 tag、GitHub Release、公开资产和下载哈希核对。" -ForegroundColor Yellow
    }
    "organize" {
        $args = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $OrganizeScript, "-WorkspaceRoot", $WorkspaceRoot)
        if ($ArchiveHistoricalWorktrees) { $args += "-ArchiveHistoricalWorktrees" }
        if ($ArchiveBuildCaches) { $args += "-ArchiveBuildCaches" }
        Invoke-Checked "pwsh" $args
    }
}

