[CmdletBinding(SupportsShouldProcess)]
param(
    [string]$WorkspaceRoot = "",
    [switch]$ArchiveHistoricalWorktrees,
    [switch]$ArchiveBuildCaches
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
    $WorkspaceRoot = Split-Path -Parent $RepoRoot
}
$WorkspaceRoot = [IO.Path]::GetFullPath($WorkspaceRoot)
$ExpectedRepo = Join-Path $WorkspaceRoot "PiInput-repo"
if ([IO.Path]::GetFullPath($RepoRoot) -ne [IO.Path]::GetFullPath($ExpectedRepo)) {
    throw "脚本所在仓库与工作区不匹配：$RepoRoot"
}

$Artifacts = Join-Path $RepoRoot "artifacts"
$Archive = Join-Path $WorkspaceRoot "archive"
$Releases = Join-Path $WorkspaceRoot "releases"

function Ensure-Directory([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
    }
}

function Assert-InWorkspace([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    $prefix = $WorkspaceRoot.TrimEnd('\') + '\'
    if (-not $full.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "路径越出 PiInput 工作区：$full"
    }
}

function Move-Into([string]$Source, [string]$DestinationDirectory) {
    if (-not (Test-Path -LiteralPath $Source)) { return }
    Assert-InWorkspace $Source
    Assert-InWorkspace $DestinationDirectory
    Ensure-Directory $DestinationDirectory
    $target = Join-Path $DestinationDirectory (Split-Path -Leaf $Source)
    if (Test-Path -LiteralPath $target) {
        $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
        $target = Join-Path $DestinationDirectory ((Split-Path -Leaf $Source) + "-$stamp")
    }
    if ($PSCmdlet.ShouldProcess($Source, "移动到 $target")) {
        Move-Item -LiteralPath $Source -Destination $target
        Write-Host "已归类：$Source -> $target" -ForegroundColor Green
    }
}

foreach ($path in @(
    $Archive,
    (Join-Path $Archive "builds"),
    (Join-Path $Archive "manual-tests"),
    (Join-Path $Archive "git-worktrees"),
    $Releases,
    (Join-Path $Releases "current"),
    (Join-Path $Releases "candidates"),
    (Join-Path $Releases "history"),
    (Join-Path $Releases "withdrawn"),
    (Join-Path $Artifacts "assets"),
    (Join-Path $Artifacts "candidates"),
    (Join-Path $Artifacts "ci\history"),
    (Join-Path $Artifacts "diagnostics"),
    (Join-Path $Artifacts "package-closure\history"),
    (Join-Path $Artifacts "package-output\current"),
    (Join-Path $Artifacts "package-output\history"),
    (Join-Path $Artifacts "soak\history"),
    (Join-Path $Artifacts "soak\failed"),
    (Join-Path $Artifacts "soak\validated"),
    (Join-Path $Artifacts "soak\fixtures"),
    (Join-Path $Artifacts "tests\current"),
    (Join-Path $Artifacts "tests\history"),
    (Join-Path $Artifacts "tests\fixtures"),
    (Join-Path $Artifacts "tests\corpus"),
    (Join-Path $Artifacts "verification\history")
)) { Ensure-Directory $path }

if ($ArchiveBuildCaches) {
    foreach ($name in @("generated-build-v033", "generated-old-build-20260811")) {
        Move-Into (Join-Path $WorkspaceRoot $name) (Join-Path $Archive "builds")
    }
    Get-ChildItem -LiteralPath $RepoRoot -Directory -Filter "build-*" -ErrorAction SilentlyContinue |
        ForEach-Object { Move-Into $_.FullName (Join-Path $Archive "builds") }
}

if ($ArchiveHistoricalWorktrees) {
    $worktreeLines = @(& git -C $RepoRoot worktree list --porcelain)
    foreach ($line in $worktreeLines) {
        if (-not $line.StartsWith("worktree ")) { continue }
        $path = $line.Substring(9)
        $name = Split-Path -Leaf $path
        if ((Split-Path -Parent $path) -ne $WorkspaceRoot) { continue }
        if ($name -notmatch '^PiInput-(candidate-|v.+-(final-)?[0-9a-f]{7,})') { continue }
        $target = Join-Path (Join-Path $Archive "git-worktrees") $name
        if ($PSCmdlet.ShouldProcess($path, "Git 工作树迁移到 $target")) {
            & git -C $RepoRoot worktree move $path $target
            if ($LASTEXITCODE -ne 0) { throw "Git 工作树迁移失败：$path" }
        }
    }
}

$entries = @(Get-ChildItem -LiteralPath $Artifacts -Force)
foreach ($item in $entries) {
    $name = $item.Name
    $destination = $null
    if ($name -match '^ci-') { $destination = Join-Path $Artifacts "ci\history" }
    elseif ($name -match '^package-closure-') { $destination = Join-Path $Artifacts "package-closure\history" }
    elseif ($name -match '^tsf-app-soak-fixture-') { $destination = Join-Path $Artifacts "soak\fixtures" }
    elseif ($name -match '^soak-8h-final-') {
        $summary = Join-Path $item.FullName "summary.json"
        if (-not (Test-Path -LiteralPath $summary)) {
            Write-Host "保留正在运行或未收尾的稳定性目录：$name" -ForegroundColor Yellow
            continue
        }
        $status = ""
        try { $status = (Get-Content -Raw -LiteralPath $summary | ConvertFrom-Json).status } catch {}
        $destination = if ($status -eq "passed") {
            Join-Path $Artifacts "soak\validated"
        } else {
            Join-Path $Artifacts "soak\failed"
        }
    }
    elseif ($name -match '^soak-') { $destination = Join-Path $Artifacts "soak\history" }
    elseif ($name -match '^(candidate-|controlled-tsf-)') { $destination = Join-Path $Artifacts "candidates" }
    elseif ($name -match '^(owner-debug|runtime-trace)$') { $destination = Join-Path $Artifacts "diagnostics" }
    elseif ($name -match '^(verify-|final-portable-check$|portable-verify$|manifest-check$)') { $destination = Join-Path $Artifacts "verification\history" }
    elseif ($name -match '^(corpus-comparison|sogou-piinput-corpus-comparison)$') { $destination = Join-Path $Artifacts "tests\corpus" }
    elseif ($name -match '^(result-composer-fixture|release-evidence-failure-selftest)') { $destination = Join-Path $Artifacts "tests\fixtures" }
    elseif ($name -match '^(human-input|test-reports|test-tools|typing-test)') { $destination = Join-Path $Artifacts "tests\history" }
    elseif ($name -eq 'test-results') { $destination = Join-Path $Artifacts "tests" }
    elseif ($name -eq 'piinput-icon-preview.png') { $destination = Join-Path $Artifacts "assets" }
    if ($null -ne $destination) { Move-Into $item.FullName $destination }
}

Write-Host "整理完成。未删除任何文件。" -ForegroundColor Cyan

