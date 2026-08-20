param(
    [int]$Seed = 0,
    [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
$corpusPath = Join-Path $repoRoot "tests/data/real_world_text_corpus.txt"
if (-not (Test-Path -LiteralPath $corpusPath -PathType Leaf)) {
    throw "Missing real-world text corpus: $corpusPath"
}
if ($Seed -eq 0) {
    $seedBytes = New-Object byte[] 4
    $seedGenerator = [Security.Cryptography.RandomNumberGenerator]::Create()
    try {
        $seedGenerator.GetBytes($seedBytes)
    } finally {
        $seedGenerator.Dispose()
    }
    $Seed = [BitConverter]::ToInt32($seedBytes, 0) -band [int]::MaxValue
    if ($Seed -eq 0) { $Seed = 1 }
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot "artifacts/human-input-scenario-$Seed"
}

$groups = @{
    1 = [System.Collections.Generic.List[string]]::new()
    2 = [System.Collections.Generic.List[string]]::new()
    3 = [System.Collections.Generic.List[string]]::new()
}
$english = [System.Collections.Generic.List[string]]::new()
$currentGroup = 0
foreach ($line in Get-Content -LiteralPath $corpusPath -Encoding UTF8) {
    if ($line -match '^#\s*测试文本\s*([123])\s*$') {
        $currentGroup = [int]$Matches[1]
        continue
    }
    if ([string]::IsNullOrWhiteSpace($line) -or $line.StartsWith('#')) { continue }
    if ($currentGroup -in 1, 2, 3 -and $line -match '\p{IsCJKUnifiedIdeographs}') {
        $groups[$currentGroup].Add($line)
    }
    if ($line -match '^[\x20-\x7E]*[A-Za-z][\x20-\x7E]*$') {
        $english.Add($line)
    }
}
foreach ($group in 1, 2, 3) {
    if ($groups[$group].Count -eq 0) { throw "Test text group $group is empty." }
}
if ($english.Count -eq 0) { throw "The corpus has no English line for mixed-input testing." }

$random = [Random]::new($Seed)
function Pick-Line([System.Collections.Generic.List[string]]$Lines) {
    return $Lines[$random.Next(0, $Lines.Count)]
}
function Clip-Text([string]$Text, [int]$RequestedLength) {
    if ($Text.Length -le $RequestedLength) { return $Text }
    return $Text.Substring(0, $RequestedLength)
}

$actions = [System.Collections.Generic.List[object]]::new()
function Add-Action([string]$Type, [string]$Text = "", [int]$SourceGroup = 0, [string]$Note = "") {
    $actions.Add([ordered]@{
        step = $actions.Count + 1
        type = $Type
        text = $Text
        source_group = $SourceGroup
        note = $Note
    })
}

$targetLengths = @(5, 9, 13, 17, 23, 31)
for ($index = 0; $index -lt $targetLengths.Count; ++$index) {
    $group = 1 + ($index % 3)
    $text = Clip-Text (Pick-Line $groups[$group]) $targetLengths[$index]
    Add-Action "type_chinese" $text $group "逐字输入；观察首键延迟、候选位置和候选稳定性"
    if ($index -in 0, 2, 4) { Add-Action "newline" "" 0 "换行后立即继续输入" }
}

$englishLine = Pick-Line $english
Add-Action "shift_toggle" "" 0 "切换到英文状态"
Add-Action "type_english" (Clip-Text $englishLine 18) 3 "短英文，检查从首字母开始补全"
Add-Action "type_english" (Clip-Text $englishLine 47) 3 "较长英文与标点"
Add-Action "shift_toggle" "" 0 "切回中文状态"
Add-Action "move_left" "" 0 "随机执行 1 到 5 次，再从中间继续输入"
Add-Action "move_right" "" 0 "随机执行 1 到 4 次"
Add-Action "move_up" "" 0 "移动到上一行后继续输入"
Add-Action "move_down" "" 0 "返回下一行"
Add-Action "paste" (Clip-Text (Pick-Line $groups[2]) 21) 2 "粘贴后立即继续输入，不得卡住或丢失焦点"
Add-Action "type_chinese" (Clip-Text (Pick-Line $groups[3]) 11) 3 "粘贴后的追加输入"
Add-Action "backspace" "" 0 "组合中与正文中分别删除 1 到 4 次"
Add-Action "delete" "" 0 "正文中删除 1 到 3 次"
Add-Action "symbol_command" ";;f" 0 "中文半角命令，打开符号中心"
Add-Action "symbol_command" "````f" 0 "反引号命令；单个和三个反引号仍须原样输入"

$scenario = [ordered]@{
    format_version = 1
    seed = $Seed
    corpus = "tests/data/real_world_text_corpus.txt"
    instructions = @(
        "在新的空白测试文档中执行，禁止覆盖用户正文。",
        "每一步记录应用、输入方案、冷/热启动耗时、候选首选、候选位置和结果。",
        "遇到失败立即截图并保留 seed；不要把 BLOCKED 或 NOT RUN 写成 PASS。"
    )
    actions = $actions
}

New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$jsonPath = Join-Path $OutputDirectory "human-input-scenario.json"
$markdownPath = Join-Path $OutputDirectory "human-input-scenario.md"
$scenario | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $jsonPath -Encoding UTF8

$markdown = [System.Collections.Generic.List[string]]::new()
$markdown.Add("# PiInput 随机真实输入场景")
$markdown.Add("")
$markdown.Add("- Seed：``$Seed``")
$markdown.Add("- 语料：``tests/data/real_world_text_corpus.txt``")
$markdown.Add("- 复现：再次使用 ``-Seed $Seed`` 生成")
$markdown.Add("")
$markdown.Add("| 步骤 | 操作 | 输入内容/说明 | 结果 |")
$markdown.Add("|---:|---|---|---|")
foreach ($action in $actions) {
    $detail = if ($action.text) { $action.text } else { $action.note }
    $detail = $detail.Replace('|', '\|').Replace("`r", '').Replace("`n", ' ')
    $markdown.Add("| $($action.step) | $($action.type) | $detail | NOT RUN |")
}
$markdown | Set-Content -LiteralPath $markdownPath -Encoding UTF8

Write-Host "Human input scenario generated."
Write-Host "Seed: $Seed"
Write-Host "JSON: $jsonPath"
Write-Host "Checklist: $markdownPath"
