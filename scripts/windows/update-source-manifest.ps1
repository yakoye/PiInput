param(
    [switch]$VerifyOnly
)

# 重建 FILE_LIST.txt 和 SHA256SUMS.txt。
#
# 收录范围就是 git 认的源码：已跟踪 + 未跟踪且未被忽略。.gitignore 已经排除了
# build/、build-*/、dist/、artifacts/、外部 dicts/ 和编译产物，所以这里不再重复维护一份
# 排除名单，避免两处规则漂移。
#
# SHA256SUMS.txt 不能收录自己，否则永远算不出稳定的哈希；FILE_LIST.txt 要收录
# 自己，并且必须先落盘再算哈希，因为它自己的哈希也在 SHA256SUMS.txt 里。
# tests/sha256_regression.cmake 会逐条复核这两条约束。

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$FileList = Join-Path $Root "FILE_LIST.txt"
$Checksums = Join-Path $Root "SHA256SUMS.txt"
$Utf8NoBom = [System.Text.UTF8Encoding]::new($false)

# 中文文件名曾经被 ASCII 编码写成问号，这里固定 UTF-8 无 BOM + LF。
function Write-ManifestFile([string]$Path, [string[]]$Lines) {
    [System.IO.File]::WriteAllText($Path, (($Lines -join "`n") + "`n"), $Utf8NoBom)
}

Push-Location $Root
try {
    $git = Get-Command git -ErrorAction SilentlyContinue
    if (-not $git) { throw "找不到 git，无法枚举源码文件。" }

    $tracked = & git ls-files --cached --others --exclude-standard
    if ($LASTEXITCODE -ne 0) { throw "git ls-files 失败，退出码 $LASTEXITCODE。" }

    $paths = @($tracked |
        Where-Object {
            $_ -ne "" -and $_ -ne "SHA256SUMS.txt" -and
            (Test-Path -LiteralPath (Join-Path $Root $_) -PathType Leaf)
        } |
        Sort-Object)
    if ($paths.Count -eq 0) { throw "枚举到 0 个源码文件，拒绝写出空清单。" }

    $checksumLines = foreach ($path in $paths) {
        $absolute = Join-Path $Root $path
        if (-not (Test-Path -LiteralPath $absolute -PathType Leaf)) {
            throw "清单条目不是文件: $path"
        }
        "{0}  {1}" -f (Get-FileHash -LiteralPath $absolute -Algorithm SHA256).Hash.ToLowerInvariant(), $path
    }

    if ($VerifyOnly) {
        $listStale = -not (Test-Path -LiteralPath $FileList) -or
            (Compare-Object ([System.IO.File]::ReadAllLines($FileList)) $paths -SyncWindow 0)
        $sumStale = -not (Test-Path -LiteralPath $Checksums) -or
            (Compare-Object ([System.IO.File]::ReadAllLines($Checksums)) @($checksumLines) -SyncWindow 0)
        if ($listStale -or $sumStale) {
            throw "FILE_LIST.txt 或 SHA256SUMS.txt 已过期，请运行 scripts/windows/update-source-manifest.ps1 重建。"
        }
        Write-Host "源码清单与实际文件一致，共 $($paths.Count) 个文件。" -ForegroundColor Green
        exit 0
    }

    # FILE_LIST.txt 收录自己，所以要先写出来，再算它的哈希。
    Write-ManifestFile $FileList $paths
    $listIndex = [array]::IndexOf($paths, "FILE_LIST.txt")
    if ($listIndex -lt 0) { throw "FILE_LIST.txt 没有出现在自己的清单里。" }
    $checksumLines = @($checksumLines)
    $checksumLines[$listIndex] = "{0}  FILE_LIST.txt" -f
        (Get-FileHash -LiteralPath $FileList -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-ManifestFile $Checksums $checksumLines

    Write-Host "已刷新源码清单，共 $($paths.Count) 个文件。" -ForegroundColor Green
    Write-Host "  FILE_LIST.txt" -ForegroundColor DarkGray
    Write-Host "  SHA256SUMS.txt" -ForegroundColor DarkGray
    exit 0
} finally {
    Pop-Location
}
