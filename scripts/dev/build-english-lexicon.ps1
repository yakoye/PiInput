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
    [string]$CommonWordList = "",
    [string]$WordFrequencyTsv = "",
    [string]$LargeWordlist = "",
    [string]$OutputTsv = "",
    [int]$MinimumLength = 3,
    [int]$MaximumLength = 14,
    # zipf 低于此值的词过于冷僻，进候选只会制造噪音。3.0 相当于每十亿词
    # 出现约一千次，palladium（3.17）刚好在线上。
    [double]$MinimumZipf = 2.5,
    # 低于此 zipf 的拼音音节判为「混进来的拼音」而非英文词，理由见 Test-Noise。
    # 实测该删的最高是 ni 3.93，该留的最低是 tan 4.01，中间空着，取 4.0。
    [double]$UsageZipfFloor = 4.0
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$WorkspaceRoot = Split-Path -Parent $RepoRoot

if ([string]::IsNullOrWhiteSpace($HighFrequencyTsv)) {
    $HighFrequencyTsv = Join-Path $RepoRoot "data\english_lexicon.high-frequency.tsv"
}
if ([string]::IsNullOrWhiteSpace($CommonWordList)) {
    $CommonWordList = Join-Path $WorkspaceRoot "dicts\english_common_words_10000plus.txt"
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

foreach ($required in @($HighFrequencyTsv, $CommonWordList, $WordFrequencyTsv, $LargeWordlist)) {
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

# 与 include/piinput/english_lexicon.h 的 EnglishCandidateFlag 对应。
$FlagDownloaded = 2             # 原表一直写 2，保持不变
$FlagCommon = 64                # 收录于「最常用两万词」表

# 「最常用两万词」表：排名 1 最常用，与原表的倒数排名方向相反。
#
# 两个用途。一是补洞：原表缺了 tablespoon、grandchild、lawmaker 这类正经常
# 用词，共 4,287 个，按各自排名补进高频层。二是打标记：三字母输入靠这个标记
# 决定给不给英文候选。
#
# 为什么是成员判定而不是权重阈值——曾经试过后者，取 1,020,000。任何一条阈线
# 都会在中间切出空集：dog(1,023,390) 过得去，而同样日常的词只要排名稍低就被
# 误伤，用户报的正是这个。这份表本身就是「最常用的两万个词」，判据和问题是
# 同一件事，不需要再挑一个数。
#
# 实测它把两组分得很干净：dog(753)、egg(1380)、cat(1785)、bus(1457)、car(289)
# 全部收录；而 tam、nim、nid、wod、niz、tzn、jiy、wom、ken 一个都不在表里。
$CommonRank = [System.Collections.Generic.Dictionary[string,int]]::new()
$commonSkipped = 0
foreach ($line in [System.IO.File]::ReadLines($CommonWordList)) {
    if ($line -notmatch '^\s*(\d+)\s+(\S+)\s*$') { continue }
    # 两个值都要在这里取走。$Matches 是全局的，下一次 -match 就会把它整个换掉，
    # 而下面还要再匹配一次——先前把 [int]$Matches[1] 留到那之后读，取到的是
    # null，于是四千多个补入词的排名全成了 0，权重挤在同一个数上。
    $rank = [int]$Matches[1]
    $word = $Matches[2].ToLowerInvariant()
    # 连字符词和撇号词（store-bought、don't）跳过，Test-Word 本来也不收。
    if ($word -notmatch '^[a-z]+$') { $commonSkipped++; continue }
    if (-not $CommonRank.ContainsKey($word)) { $CommonRank[$word] = $rank }
}
Write-Host "常用词表：$($CommonRank.Count) 词（跳过非纯字母 $commonSkipped 条）"

function Test-Word([string]$Word) {
    return $Word.Length -ge $MinimumLength -and
           $Word.Length -le $MaximumLength -and
           $Word -match '^[a-z]+$'
}

# 汉语拼音混进了英语词频表——wordfreq 统计真实文本，中文拼音在英文语境里
# 出现得不少——于是 hao、xie、dui、zhong 这些都成了「英文词」。它们和 car、
# cat 一样落在高频段，靠权重分不开，在中文候选行里却纯属噪音。
#
# 判据是两条同时成立：是合法拼音音节，且在英语里根本没人用。
#
# 后一条不能省。大量常用英文词本身就是合法音节——can、he、me、man、men、
# run、sun、gun、fan、ban、pan、tan 全是——只按音节剔会把它们一并删掉。
# 4.0 这个界不是估的，是量出来的：该删的最高是 ni 3.93，该留的最低是
# tan 4.01，中间空着。
#
# 前一条同样不能省，虽然一度想改成「短词必须常用」。那条规则会顺手删掉
# wane(2.83) 和 dow(3.63)——两个货真价实的英文词。这份词库英文模式也在用，
# 删了它们等于英文模式也打不出来，代价远超收益。
#
# 于是 nim(2.82)、tam(3.28)、wod(2.44)、nid(2.53) 这类既不是音节、又没人用
# 的短词留在了词库里。它们不需要在这里处理：三字母输入要求词带 $FlagCommon，
# 而这四个词一个都不在常用词表里，english_completion.cpp 的 is_common_word
# 会把它们挡住。
#
# 同理，ta(4.02)、le(4.50)、de(5.23) 是音节但高于门槛，留了下来；它们都是
# 两个字母，而 kMinimumLength 要求三个字母起步，中文模式下取不到。
$PinyinSyllables = [System.Collections.Generic.HashSet[string]]::new()
foreach ($line in [System.IO.File]::ReadLines((Join-Path $RepoRoot "src\pinyin_syllables.inc"))) {
    if ($line -match '"([a-z]+)"') { [void]$PinyinSyllables.Add($Matches[1]) }
}
Write-Host "拼音音节表：$($PinyinSyllables.Count) 个"

function Test-Noise([string]$Word, [double]$WordZipf) {
    if (-not $PinyinSyllables.Contains($Word)) { return $false }
    # 常用词表收录的词一律放行。zipf 阈值本来就是在替「这是不是一个英语词」
    # 做判断，而那份表是人工整理的答案，比阈值权威。
    #
    # 不放行就会误删 nun、wan、hen、shun、chi、fang、pang、gong——全是货真价实
    # 的英文词，只因为同时是拼音音节、zipf 又低于 4.0 而被剔掉。
    #
    # 放行不会把噪音带回来：真正的拼音混入词 hao、xie、dui、mei、shi、wo、ni、
    # bu、zhong、shang 一个都不在常用表里。表里唯一与之重叠的是 ta（排名
    # 11,056），而它只有两个字母，kMinimumLength 要求三个字母起步，中文模式下
    # 取不到。
    if ($CommonRank.ContainsKey($Word)) { return $false }
    return $WordZipf -lt $UsageZipfFloor
}

# 高频层自身第二列是排名不是词频，判定噪音时得回查真实 zipf。
$zipfIndex = [System.Collections.Generic.Dictionary[string,double]]::new()
foreach ($line in [System.IO.File]::ReadLines($WordFrequencyTsv)) {
    $parts = $line -split "`t"
    if ($parts.Count -ge 2) { $zipfIndex[$parts[0]] = [double]$parts[1] }
}
$dropped = New-Object System.Collections.Generic.List[string]

function Get-Zipf([string]$Word) {
    if ($zipfIndex.ContainsKey($Word)) { return $zipfIndex[$Word] }
    # 词频表里根本没有，等于没人用，按零处理。
    return 0.0
}

# --- 高频层 ---
function Get-Flags([string]$Word) {
    if ($CommonRank.ContainsKey($Word)) { return $FlagDownloaded -bor $FlagCommon }
    return $FlagDownloaded
}

$high = [System.Collections.Generic.Dictionary[string,int]]::new()
$rows = New-Object System.Collections.Generic.List[string]
foreach ($line in [System.IO.File]::ReadLines($HighFrequencyTsv)) {
    if ([string]::IsNullOrWhiteSpace($line)) { continue }
    $parts = $line -split "`t"
    if ($parts.Count -lt 2) { continue }
    $word = $parts[0].ToLowerInvariant()
    if ($word -notmatch '^[a-z]+$') { continue }
    if ($high.ContainsKey($word)) { continue }
    if (Test-Noise $word (Get-Zipf $word)) { $dropped.Add($word); continue }
    $high[$word] = [int]$parts[1]
}
$fromOriginal = $high.Count

# 常用词表里原表没收的词，按各自排名补进同一层。两张表的排名方向相反：原表
# the=24,323 递减，常用表 the=1 递增，所以要翻过来对齐。
$maxRank = 0
foreach ($rank in $high.Values) { if ($rank -gt $maxRank) { $maxRank = $rank } }
$added = 0
foreach ($entry in $CommonRank.GetEnumerator()) {
    $word = $entry.Key
    # 只要求纯字母，和上面读原表时同一条规则。这里一度用了 Test-Word，于是
    # $MaximumLength 把 environmentalist、counterproductive 这类词挡在外面——
    # 而原表自己就收着 representatives、characteristics 等 66 个超长词，同一层
    # 里两套标准说不通。长度上限是给后面两层的批量词表用的。
    if ($word -notmatch '^[a-z]+$') { continue }
    if ($high.ContainsKey($word)) { continue }
    if (Test-Noise $word (Get-Zipf $word)) { $dropped.Add($word); continue }
    # 常用表排名 1 对应原表的最高位。排名超出原表长度的落到 1，仍在高频段内。
    $high[$word] = [Math]::Max(1, $maxRank + 1 - $entry.Value)
    $added++
}
Write-Host "高频层：$($high.Count) 词（原表 $fromOriginal + 常用表补入 $added）"

# 两个文件都从这一份排序结果写出，顺序因此只取决于内容，不取决于词是从原表
# 读来的还是从常用表补的。分两处写过一版，结果是重跑一次行序就变——内容没
# 变而 SHA256 变了，piinput-sha256-regression 每次重建都会失败。
$ordered = $high.GetEnumerator() | Sort-Object `
    @{ Expression = { $_.Value }; Descending = $true },
    @{ Expression = { $_.Key }; Descending = $false }

# 合并后的高频层写回自己的输入文件，这样「高频词汇」始终只有一份数据，而不是
# 一张原表加一张待合并的清单。
#
# 重跑是稳定的：补入的词已经在文件里，第二遍 $high.ContainsKey 就跳过了，不会
# 再分配一次排名。代价是原表的初始内容只留在 git 历史里——被拼音过滤剔掉的词
# 也一并不再出现，那正是想要的结果。
$mergedRows = New-Object System.Collections.Generic.List[string]
foreach ($pair in $ordered) {
    $mergedRows.Add(("{0}`t{1}" -f $pair.Key, $pair.Value))
    $rows.Add(("{0}`t{1}`t{2}" -f $pair.Key,
        ($HighFrequencyBase + $pair.Value), (Get-Flags $pair.Key)))
}
[System.IO.File]::WriteAllLines($HighFrequencyTsv, $mergedRows)
Write-Host "已写回 $HighFrequencyTsv（$($mergedRows.Count) 词，按排名降序去重）"

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
    if (Test-Noise $word $zipf) { $dropped.Add($word); continue }
    if (-not $seen.Add($word)) { continue }
    $mid.Add([pscustomobject]@{ Word = $word; Zipf = $zipf })
}
# 词频表本身已按 zipf 降序，这里保持读入顺序即可，分数依次递减。
$score = $MidFrequencyCeiling
foreach ($entry in $mid) {
    if ($score -le $MidFrequencyBase) { break }
    $rows.Add(("{0}`t{1}`t{2}" -f $entry.Word, $score, (Get-Flags $entry.Word)))
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
    # 这一层的词多半连词频记录都没有，Get-Zipf 返回 0，音节一律剔除。
    if (Test-Noise $word (Get-Zipf $word)) { $dropped.Add($word); continue }
    $low.Add($word)
}
# 短词优先，同长按字母序，保证同一份输入每次生成同样的文件。
$orderedLow = $low | Sort-Object @{Expression = { $_.Length }}, @{Expression = { $_ }}
$score = $LowFrequencyCeiling
foreach ($word in $orderedLow) {
    if ($score -lt 1) { break }
    $rows.Add(("{0}`t{1}`t{2}" -f $word, $score, (Get-Flags $word)))
    $score--
}
Write-Host "低频层：$($LowFrequencyCeiling - $score) 词"

Write-Host "剔除噪音词：$($dropped.Count) 个"

[System.IO.File]::WriteAllLines($OutputTsv, $rows)
$info = Get-Item -LiteralPath $OutputTsv
Write-Host ("已写出 {0}，共 {1} 行，{2:N2} MB" -f $OutputTsv, $rows.Count, ($info.Length / 1MB))
