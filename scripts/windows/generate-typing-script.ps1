# Turns the three real-world test texts into keystroke scripts for
# piinput-host-client-fixture.exe --type-script.
#
# Each output line is one thing the user types before committing:
#   <xiaohe code>\t<wanted text>\t<trailing punctuation key or ->\t<0|1 shifted>
#
# The fixture types the code letter by letter, then finds the wanted text in the
# live candidate page and presses '=' / a digit to select it. That is why the
# wanted text is carried here instead of assuming candidate 1 is always right.
param(
    [string]$DictionaryRoot = "",
    [string]$OutputDirectory = "",
    [int]$MaxChunkCharacters = 12
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if ([string]::IsNullOrWhiteSpace($DictionaryRoot)) {
    $DictionaryRoot = Join-Path (Split-Path -Parent $RepoRoot) "dicts"
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $RepoRoot "artifacts/typing-test"
}
$GeneratedTsv = Join-Path $DictionaryRoot "generated/piinput-combined.tsv"
$Corpus = Join-Path $RepoRoot "tests/data/real_world_text_corpus.txt"
$XiaoheSchemePath = Join-Path $RepoRoot "tests/corpus/v0.2.0/schemes/xiaohe.json"
foreach ($path in @($GeneratedTsv, $Corpus, $XiaoheSchemePath)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing input: $path" }
}
$xiaoheScheme = Get-Content -Raw -LiteralPath $XiaoheSchemePath -Encoding UTF8 | ConvertFrom-Json

function Get-MapValue([object]$Map, [string]$Key) {
    $property = $Map.PSObject.Properties[$Key]
    if ($null -eq $property) { return $null }
    return [string]$property.Value
}

function Convert-SyllableToXiaohe([string]$Syllable) {
    $normalized = $Syllable.ToLowerInvariant().Replace([char]0x00FC, 'v')
    $zero = Get-MapValue $xiaoheScheme.zero_initial_map $normalized
    if ($null -ne $zero) { return $zero }
    $initial = ""
    foreach ($candidate in @('zh', 'ch', 'sh')) {
        if ($normalized.StartsWith($candidate)) { $initial = $candidate; break }
    }
    if (-not $initial -and $normalized.Length -gt 0 -and
        'bpmfdtnlgkhjqxrzcsyw'.Contains($normalized[0])) {
        $initial = [string]$normalized[0]
    }
    if (-not $initial) { return $null }
    $final = $normalized.Substring($initial.Length)
    $initialCode = Get-MapValue $xiaoheScheme.initial_map $initial
    $finalCode = Get-MapValue $xiaoheScheme.final_map $final
    if ($null -eq $initialCode -or $null -eq $finalCode) { return $null }
    return $initialCode + $finalCode
}

function Convert-PinyinToXiaohe([string]$Pinyin) {
    $codes = ""
    foreach ($syllable in $Pinyin -split "'") {
        if (-not $syllable) { continue }
        $code = Convert-SyllableToXiaohe $syllable
        if ($null -eq $code) { return $null }
        $codes += $code
    }
    return $codes
}

# The Chinese punctuation a user actually reaches through a physical key while
# Chinese punctuation is on. Anything else is dropped from the script.
$punctuationKeys = @{
    [char]0xFF0C = @{ Key = ','; Shift = 0 }   # ，
    [char]0x3002 = @{ Key = '.'; Shift = 0 }   # 。
    [char]0xFF1F = @{ Key = '/'; Shift = 1 }   # ？
    [char]0xFF01 = @{ Key = '1'; Shift = 1 }   # ！
    [char]0x3001 = @{ Key = '/'; Shift = 0 }   # 、
    [char]0xFF1B = @{ Key = ';'; Shift = 0 }   # ；
    [char]0xFF1A = @{ Key = ';'; Shift = 1 }   # ：
}

Write-Host "Building reverse word-to-pinyin index..." -ForegroundColor Cyan
$entries = @{}
Get-Content -LiteralPath $GeneratedTsv -Encoding UTF8 | Select-Object -Skip 1 | ForEach-Object {
    $columns = $_ -split "`t"
    if ($columns.Count -ge 3) {
        $weight = 0L
        if ([long]::TryParse($columns[2], [ref]$weight)) {
            $existing = $entries[$columns[0]]
            if ($null -eq $existing -or $weight -gt $existing.Weight) {
                $entries[$columns[0]] = [pscustomobject]@{ Pinyin = $columns[1]; Weight = $weight }
            }
        }
    }
}
Write-Host "Indexed $($entries.Count) words." -ForegroundColor Cyan

function Get-Chunks([string]$Clause) {
    $clean = [regex]::Replace($Clause, '[^\p{IsCJKUnifiedIdeographs}]', '')
    $tokens = [System.Collections.Generic.List[object]]::new()
    $position = 0
    while ($position -lt $clean.Length) {
        $matched = $false
        $remaining = $clean.Length - $position
        for ($length = [Math]::Min(8, $remaining); $length -ge 1; --$length) {
            $word = $clean.Substring($position, $length)
            if ($entries.ContainsKey($word)) {
                $tokens.Add([pscustomobject]@{ Word = $word; Pinyin = $entries[$word].Pinyin })
                $position += $length
                $matched = $true
                break
            }
        }
        if (-not $matched) { ++$position }
    }
    $chunks = [System.Collections.Generic.List[object]]::new()
    $words = [System.Collections.Generic.List[string]]::new()
    $pinyin = [System.Collections.Generic.List[string]]::new()
    $length = 0
    foreach ($token in $tokens) {
        if ($length -gt 0 -and $length + $token.Word.Length -gt $MaxChunkCharacters) {
            $chunks.Add([pscustomobject]@{ Target = ($words -join ''); Pinyin = ($pinyin -join "'") })
            $words.Clear(); $pinyin.Clear(); $length = 0
        }
        $words.Add($token.Word)
        $pinyin.Add($token.Pinyin)
        $length += $token.Word.Length
    }
    if ($words.Count -gt 0) {
        $chunks.Add([pscustomobject]@{ Target = ($words -join ''); Pinyin = ($pinyin -join "'") })
    }
    return $chunks
}

New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$currentGroup = 0
$groupLines = @{ 1 = [System.Collections.Generic.List[string]]::new()
                 2 = [System.Collections.Generic.List[string]]::new()
                 3 = [System.Collections.Generic.List[string]]::new() }
$groupText = @{ 1 = [Text.StringBuilder]::new()
                2 = [Text.StringBuilder]::new()
                3 = [Text.StringBuilder]::new() }

foreach ($line in Get-Content -LiteralPath $Corpus -Encoding UTF8) {
    if ($line -match '^#\s*测试文本\s*([123])\s*$') { $currentGroup = [int]$Matches[1]; continue }
    if ($currentGroup -eq 0) { continue }
    if ([string]::IsNullOrWhiteSpace($line) -or $line.StartsWith('#')) { continue }
    if ($line -notmatch '\p{IsCJKUnifiedIdeographs}') { continue }

    # Split into clauses but keep the punctuation that ends each one, because
    # that key is what commits the clause during real typing.
    $buffer = ""
    foreach ($character in $line.ToCharArray()) {
        if ($punctuationKeys.ContainsKey($character)) {
            $mapping = $punctuationKeys[$character]
            $chunks = @(Get-Chunks $buffer)
            for ($index = 0; $index -lt $chunks.Count; ++$index) {
                $code = Convert-PinyinToXiaohe $chunks[$index].Pinyin
                if ([string]::IsNullOrWhiteSpace($code)) { continue }
                $isLast = ($index -eq $chunks.Count - 1)
                $key = if ($isLast) { $mapping.Key } else { '-' }
                $shift = if ($isLast) { $mapping.Shift } else { 0 }
                $groupLines[$currentGroup].Add(
                    ($code + "`t" + $chunks[$index].Target + "`t" + $key + "`t" + $shift))
                [void]$groupText[$currentGroup].Append($chunks[$index].Target)
                if ($isLast) { [void]$groupText[$currentGroup].Append($character) }
            }
            $buffer = ""
            continue
        }
        $buffer += $character
    }
    if ($buffer.Trim()) {
        foreach ($chunk in @(Get-Chunks $buffer)) {
            $code = Convert-PinyinToXiaohe $chunk.Pinyin
            if ([string]::IsNullOrWhiteSpace($code)) { continue }
            $groupLines[$currentGroup].Add(($code + "`t" + $chunk.Target + "`t-`t0"))
            [void]$groupText[$currentGroup].Append($chunk.Target)
        }
    }
}

$utf8 = New-Object Text.UTF8Encoding $false
$total = 0
$totalKeys = 0
foreach ($group in 1, 2, 3) {
    $lines = $groupLines[$group]
    if ($lines.Count -eq 0) { throw "Test text group $group produced no keystrokes." }
    $scriptPath = Join-Path $OutputDirectory "text-$group.keys.tsv"
    $expectedPath = Join-Path $OutputDirectory "text-$group.expected.txt"
    [IO.File]::WriteAllText($scriptPath, (($lines -join "`n") + "`n"), $utf8)
    [IO.File]::WriteAllText($expectedPath, $groupText[$group].ToString(), $utf8)
    $keys = 0
    foreach ($entry in $lines) {
        $fields = $entry -split "`t"
        $keys += $fields[0].Length
        if ($fields[2] -ne '-') { $keys += 1 }
    }
    $total += $lines.Count
    $totalKeys += $keys
    Write-Host ("text {0}: {1} clauses, >= {2} letter/punctuation keys -> {3}" -f `
        $group, $lines.Count, $keys, $scriptPath) -ForegroundColor Green
}
Write-Host "total: $total clauses, >= $totalKeys keys before candidate selection." -ForegroundColor Cyan
