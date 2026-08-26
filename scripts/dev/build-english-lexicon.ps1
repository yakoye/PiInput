# 把英文候选词库合并成三层。
#
# 高频层是原有的 24,323 词表。它的第二列是排名倒数而不是真实词频
# （the=24323、of=24322、codex=5163 即第 19161 名），所以整体抬到
# 1,000,000 之上单独占一段，保证日常高频词永远压过后面两层。
#
# 中频层来自 wordfreq 的英语 zipf 词频。它是拼写辅助能不能用的关键：
# 光有词形不够，palladium 会被 palladia、palladic 这类同前缀的生僻词按
# 词长挡住，而这两个词在词频表里根本不存在——正说明它们没人用。
#
# 低频层是 dwyl/english-words 里连词频表都没收录的词，只保证"打得出来"，
# 排在最后。
#
# 数据来源与许可：
#   dwyl/english-words  Unlicense（公有领域）
#   wordfreq            数据 CC-BY-SA 4.0，需署名
[CmdletBinding()]
param(
    [string]$HighFrequencyTsv = "",
    [string]$WordFrequencyTsv = "",
    [string]$LargeWordlist = "",
    [string]$OutputTsv = "",
    [int]$MinimumLength = 3,
    [int]$MaximumLength = 14,
    # zipf 低于此值的词过于冷僻，进候选只会制造噪音。3.0 相当于每十亿词
    # 出现约一千次，palladium（3.17）刚好在线上。
    [double]$MinimumZipf = 2.5
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$WorkspaceRoot = Split-Path -Parent $RepoRoot

if ([string]::IsNullOrWhiteSpace($HighFrequencyTsv)) {
    $HighFrequencyTsv = Join-Path $RepoRoot "data\english_lexicon.high-frequency.tsv"
}
if ([string]::IsNullOrWhiteSpace($WordFrequencyTsv)) {
    $WordFrequencyTsv = Join-Path $WorkspaceRoot "dicts\sources\wordfreq-en-200000\wordfreq-en.tsv"
}
if ([string]::IsNullOrWhiteSpace($LargeWordlist)) {
    $LargeWordlist = Join-Path $WorkspaceRoot "dicts\sources\dwyl-english-words\words_alpha.txt"
}
if ([string]::IsNullOrWhiteSpace($OutputTsv)) {
    $OutputTsv = Join-Path $RepoRoot "data\english_lexicon.tsv"
}

foreach ($required in @($HighFrequencyTsv, $WordFrequencyTsv, $LargeWordlist)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "缺少输入文件：$required"
    }
}

# 三段互不重叠的分数区间。原表最大排名 24,323，因此高频层落在
# 1,000,001 ~ 1,024,323。
$HighFrequencyBase = 1000000
$MidFrequencyBase = 200000      # 中频层 200,001 ~ 999,999
$MidFrequencyCeiling = 999999
$LowFrequencyCeiling = 199999   # 低频层 1 ~ 199,999

function Test-Word([string]$Word) {
    return $Word.Length -ge $MinimumLength -and
           $Word.Length -le $MaximumLength -and
           $Word -match '^[a-z]+$'
}

# --- 高频层 ---
$high = [System.Collections.Generic.Dictionary[string,int]]::new()
$rows = New-Object System.Collections.Generic.List[string]
foreach ($line in [System.IO.File]::ReadLines($HighFrequencyTsv)) {
    if ([string]::IsNullOrWhiteSpace($line)) { continue }
    $parts = $line -split "`t"
    if ($parts.Count -lt 2) { continue }
    $word = $parts[0].ToLowerInvariant()
    if ($word -notmatch '^[a-z]+$') { continue }
    if ($high.ContainsKey($word)) { continue }
    $high[$word] = [int]$parts[1]
    $rows.Add(("{0}`t{1}`t2" -f $word, ($HighFrequencyBase + [int]$parts[1])))
}
Write-Host "高频层：$($high.Count) 词"

# --- 中频层：有真实词频的词，按 zipf 排序 ---
$mid = [System.Collections.Generic.List[object]]::new()
$seen = [System.Collections.Generic.HashSet[string]]::new()
foreach ($line in [System.IO.File]::ReadLines($WordFrequencyTsv)) {
    if ([string]::IsNullOrWhiteSpace($line)) { continue }
    $parts = $line -split "`t"
    if ($parts.Count -lt 2) { continue }
    $word = $parts[0]
    if (-not (Test-Word $word)) { continue }
    if ($high.ContainsKey($word)) { continue }
    $zipf = [double]$parts[1]
    if ($zipf -lt $MinimumZipf) { continue }
    if (-not $seen.Add($word)) { continue }
    $mid.Add([pscustomobject]@{ Word = $word; Zipf = $zipf })
}
# 词频表本身已按 zipf 降序，这里保持读入顺序即可，分数依次递减。
$score = $MidFrequencyCeiling
foreach ($entry in $mid) {
    if ($score -le $MidFrequencyBase) { break }
    $rows.Add(("{0}`t{1}`t2" -f $entry.Word, $score))
    $score--
}
$midWritten = $MidFrequencyCeiling - $score
Write-Host "中频层：$midWritten 词（zipf >= $MinimumZipf）"

# --- 低频层：连词频表都没有的词，只保证打得出来 ---
$low = New-Object System.Collections.Generic.List[string]
foreach ($line in [System.IO.File]::ReadLines($LargeWordlist)) {
    $word = $line.Trim().ToLowerInvariant()
    if (-not (Test-Word $word)) { continue }
    if ($high.ContainsKey($word)) { continue }
    if ($seen.Contains($word)) { continue }
    $low.Add($word)
}
# 短词优先，同长按字母序，保证同一份输入每次生成同样的文件。
$orderedLow = $low | Sort-Object @{Expression = { $_.Length }}, @{Expression = { $_ }}
$score = $LowFrequencyCeiling
foreach ($word in $orderedLow) {
    if ($score -lt 1) { break }
    $rows.Add(("{0}`t{1}`t2" -f $word, $score))
    $score--
}
Write-Host "低频层：$($LowFrequencyCeiling - $score) 词"

[System.IO.File]::WriteAllLines($OutputTsv, $rows)
$info = Get-Item -LiteralPath $OutputTsv
Write-Host ("已写出 {0}，共 {1} 行，{2:N2} MB" -f $OutputTsv, $rows.Count, ($info.Length / 1MB))
