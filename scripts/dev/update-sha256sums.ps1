# 按 SHA256SUMS.txt 现有的文件清单重算校验和。
#
# 清单的用途是发现「文件变了但没人打算改它」。它的代价是每次有意修改都要手工
# 同步，而这一步一旦漏掉，piinput-sha256-regression 会在下一次全量测试里失败，
# 失败信息只说哪个文件对不上，不说是谁改的——f98c9bb 就是这么漏的。
#
# 这个脚本只重算已在清单里的条目，不会自己添加或删除文件：新增文件该不该进清单
# 是个判断，不该由脚本替你做。用 -WhatIf 先看会改哪些。
[CmdletBinding(SupportsShouldProcess)]
param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
)

$ErrorActionPreference = "Stop"

$manifest = Join-Path $Root "SHA256SUMS.txt"
if (-not (Test-Path -LiteralPath $manifest)) {
    throw "SHA256SUMS.txt not found under $Root"
}

$updated = [System.Collections.Generic.List[string]]::new()
$missing = [System.Collections.Generic.List[string]]::new()
$output = [System.Collections.Generic.List[string]]::new()

foreach ($line in [System.IO.File]::ReadAllLines($manifest)) {
    if ($line -notmatch '^([0-9a-fA-F]{64})  (.+)$') {
        $output.Add($line)
        continue
    }
    # $Matches 会被后续任何一次 -match 覆盖，所以先取干净。
    $recorded = $Matches[1].ToLowerInvariant()
    $relative = $Matches[2] -replace '\\', '/'

    $full = Join-Path $Root $relative
    if (-not (Test-Path -LiteralPath $full -PathType Leaf)) {
        $missing.Add($relative)
        $output.Add($line)
        continue
    }
    $actual = (Get-FileHash -LiteralPath $full -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $recorded) { $updated.Add($relative) }
    $output.Add("$actual  $relative")
}

if ($missing.Count -gt 0) {
    Write-Warning "清单里有 $($missing.Count) 个文件不存在，条目保持原样："
    foreach ($item in $missing) { Write-Warning "  $item" }
}

if ($updated.Count -eq 0) {
    Write-Host "SHA256SUMS.txt 已是最新，无需改动。"
    return
}

Write-Host "将更新 $($updated.Count) 条："
foreach ($item in $updated) { Write-Host "  $item" }

if ($PSCmdlet.ShouldProcess($manifest, "重写校验和")) {
    # 明确写 LF：WriteAllLines 在 Windows 上用 CRLF，而仓库里存的是 LF，
    # 每跑一次就把整个文件的行尾翻一遍，真正改了哪几行反而看不出来。
    [System.IO.File]::WriteAllText($manifest, ($output -join "`n") + "`n")
    Write-Host "已写入 $manifest"
}
