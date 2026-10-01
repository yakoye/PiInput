param(
    [switch]$VerifyOnly,
    # 清单变短时的显式确认。见下面的收缩保险。
    [switch]$AllowShrink
)

# 重建 FILE_LIST.txt 和 SHA256SUMS.txt。
#
# 收录范围就是 git 认的源码：已跟踪 + 未跟踪且未被忽略。.gitignore 已经排除了
# build/、build-*/、dist/、artifacts/、外部 dicts/ 和编译产物，所以这里不再重复维护一份
# 排除名单，避免两处规则漂移。
#
# SHA256SUMS.txt 不能收录自己，否则永远算不出稳定的哈希；FILE_LIST.txt 要收录
# 自己，并且必须先落盘再算哈希，因为它自己的哈希也在 SHA256SUMS.txt 里。
# tests/regression/sha256_regression.cmake 会逐条复核这两条约束。

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

    # core.quotepath=false 与 -z 缺一不可，否则中文文件名会被静默漏收。
    #
    # git 默认 core.quotepath=true，对非 ASCII 路径输出 "docs/\345\276\205...\.md"
    # 这种带引号的八进制转义形式；下面的 Test-Path 对它一律失败，而 Where-Object
    # 把失败的条目直接滤掉——于是 docs/ 下所有中文名文件消失，脚本照常报告成功。
    # 实测这会把 FILE_LIST.txt 从 711 条削到 462 条。-z 则避免路径里的换行被当成
    # 分隔符，顺带省掉 git 对含特殊字符路径的加引号处理。
    $previousEncoding = [Console]::OutputEncoding
    [Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
    try {
        $raw = & git -c core.quotepath=false ls-files -z --cached --others --exclude-standard
    } finally {
        [Console]::OutputEncoding = $previousEncoding
    }
    if ($LASTEXITCODE -ne 0) { throw "git ls-files 失败，退出码 $LASTEXITCODE。" }
    $tracked = ($raw -join "") -split "`0"

    $skipped = @()
    $paths = @($tracked |
        Where-Object { $_ -ne "" -and $_ -ne "SHA256SUMS.txt" } |
        Where-Object {
            if (Test-Path -LiteralPath (Join-Path $Root $_) -PathType Leaf) { return $true }
            # 目录条目（子模块）会走到这里，是正常的；路径解析不了也会走到这里，
            # 那就不正常。两者都记下来，下面的收缩保险负责判断要不要拦。
            $script:skipped += $_
            return $false
        })
    # 序数排序，不用 Sort-Object 的区域感知比较：那会让同一个仓库在不同语言环境
    # 的机器上生成顺序不同的清单，文件无谓地来回变动。
    [System.Array]::Sort($paths, [System.StringComparer]::Ordinal)
    if ($paths.Count -eq 0) { throw "枚举到 0 个源码文件，拒绝写出空清单。" }

    # 收缩保险。上面那个缺陷最糟的地方不是算错，是**报告成功**——清单少了 249 条
    # 而没有任何提示，要等到别人校验失败才发现。清单变短一律先拦下来。
    if (-not $VerifyOnly -and (Test-Path -LiteralPath $FileList)) {
        $existing = @([System.IO.File]::ReadAllLines($FileList) | Where-Object { $_ -ne "" })
        if ($paths.Count -lt $existing.Count -and -not $AllowShrink) {
            $lost = @(Compare-Object $existing $paths |
                Where-Object { $_.SideIndicator -eq "<=" } |
                ForEach-Object { $_.InputObject })
            $sample = ($lost | Select-Object -First 5) -join "`n    "
            throw ("清单会从 $($existing.Count) 条缩到 $($paths.Count) 条，拒绝写出。`n" +
                "  少掉的前几条：`n    $sample`n" +
                "  确属有意删除文件就加 -AllowShrink 重跑。")
        }
    }

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
