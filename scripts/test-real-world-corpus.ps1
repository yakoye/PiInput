param(
    [string]$DictionaryRoot = "",
    [ValidateRange(0, 100)]
    [int]$RequiredHitRate = 0
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$RepoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($DictionaryRoot)) {
    $DictionaryRoot = Join-Path (Split-Path -Parent $RepoRoot) "dicts"
}
$GeneratedTsv = Join-Path $DictionaryRoot "generated/piinput-combined.tsv"
$Lexicon = Join-Path $DictionaryRoot "cache/piinput-base.lex"
$Cli = Join-Path $RepoRoot "dist/windows-x64/bin/piinput-cli.exe"
$Corpus = Join-Path $RepoRoot "tests/data/real_world_text_corpus.txt"
$ReportDirectory = Join-Path $DictionaryRoot "tests"
$Report = Join-Path $ReportDirectory "real-world-corpus-results.tsv"
foreach ($path in @($GeneratedTsv, $Lexicon, $Cli, $Corpus)) {
    if (-not (Test-Path $path)) { throw "Missing corpus test input: $path" }
}

Write-Host "Preparing reverse word-to-pinyin index..." -ForegroundColor Cyan
$entries = @{}
Get-Content $GeneratedTsv -Encoding UTF8 | Select-Object -Skip 1 | ForEach-Object {
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

function Convert-ToChunks([string]$Text) {
    $clean = [regex]::Replace($Text, '[^\p{IsCJKUnifiedIdeographs}]', '')
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
        if ($length -gt 0 -and $length + $token.Word.Length -gt 14) {
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

New-Item $ReportDirectory -ItemType Directory -Force | Out-Null
$results = [System.Collections.Generic.List[object]]::new()
$paragraph = 0
Get-Content $Corpus -Encoding UTF8 | Where-Object { $_ -and -not $_.StartsWith('#') } | ForEach-Object {
    ++$paragraph
    $clauses = $_ -split '[，。！？；：、“”‘’（）]+'
    foreach ($clause in $clauses) {
        foreach ($chunk in Convert-ToChunks $clause) {
            if ([string]::IsNullOrWhiteSpace($chunk.Pinyin)) { continue }
            $input = $chunk.Pinyin -replace "'", ''
            $output = (& $Cli --lexicon $Lexicon --schema full --query $input --top 10 2>&1) | Out-String
            $rank = 0
            foreach ($line in $output -split "`r?`n") {
                if ($line -match '^(\d+)\.\s+([^\t]+)\t' -and $Matches[2] -eq $chunk.Target) {
                    $rank = [int]$Matches[1]
                    break
                }
            }
            $results.Add([pscustomobject]@{
                Paragraph = $paragraph
                Target = $chunk.Target
                FullPinyin = $input
                Rank = $rank
                Hit = ($rank -gt 0)
            })
        }
    }
}

$results | ForEach-Object {
    "$($_.Paragraph)`t$($_.Target)`t$($_.FullPinyin)`t$($_.Rank)`t$($_.Hit)"
} | Set-Content $Report -Encoding UTF8
$total = $results.Count
$hits = @($results | Where-Object Hit).Count
$rate = if ($total -eq 0) { 0 } else { [Math]::Round(100.0 * $hits / $total, 2) }
Write-Host "Real-world corpus: $hits/$total chunks hit Top 10 ($rate%)." -ForegroundColor Green
Write-Host "Report: $Report" -ForegroundColor Cyan
if ($rate -lt $RequiredHitRate) {
    throw "Corpus hit rate $rate% is below required $RequiredHitRate%."
}
