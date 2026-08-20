param(
    [string]$DictionaryRoot = "",
    [string]$CliPath = "",
    [string]$ReportPath = "",
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
$Cli = if ([string]::IsNullOrWhiteSpace($CliPath)) {
    Join-Path $RepoRoot "dist/windows-x64/bin/piinput-cli.exe"
} else {
    $CliPath
}
$Corpus = Join-Path $RepoRoot "tests/data/real_world_text_corpus.txt"
$XiaoheSchemePath = Join-Path $RepoRoot "tests/corpus/v0.2.0/schemes/xiaohe.json"
$ReportDirectory = Join-Path $DictionaryRoot "tests"
$Report = if ([string]::IsNullOrWhiteSpace($ReportPath)) {
    Join-Path $ReportDirectory "real-world-corpus-results.tsv"
} else {
    $ReportPath
}
foreach ($path in @($GeneratedTsv, $Lexicon, $Cli, $Corpus, $XiaoheSchemePath)) {
    if (-not (Test-Path $path)) { throw "Missing corpus test input: $path" }
}
$xiaoheScheme = Get-Content -Raw -LiteralPath $XiaoheSchemePath -Encoding UTF8 | ConvertFrom-Json

function Get-MapValue([object]$Map, [string]$Key) {
    $property = $Map.PSObject.Properties[$Key]
    if ($null -eq $property) { return $null }
    return [string]$property.Value
}

function Convert-SyllableToXiaohe([string]$Syllable) {
    $normalized = $Syllable.ToLowerInvariant().Replace('ü', 'v')
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
    if (-not $initial) { throw "Unsupported zero-initial syllable: $Syllable" }
    $final = $normalized.Substring($initial.Length)
    $initialCode = Get-MapValue $xiaoheScheme.initial_map $initial
    $finalCode = Get-MapValue $xiaoheScheme.final_map $final
    if ($null -eq $initialCode -or $null -eq $finalCode) {
        throw "Cannot encode Xiaohe syllable: $Syllable (initial=$initial final=$final)"
    }
    return $initialCode + $finalCode
}

function Convert-PinyinToXiaohe([string]$Pinyin) {
    $codes = foreach ($syllable in $Pinyin -split "'") {
        if ($syllable) { Convert-SyllableToXiaohe $syllable }
    }
    return $codes -join ''
}

function Find-CandidateRank([string]$Schema, [string]$Query, [string]$Target) {
    $output = (& $Cli --lexicon $Lexicon --schema $Schema --query $Query --top 10 2>&1) | Out-String
    foreach ($line in $output -split "`r?`n") {
        if ($line -match '^(\d+)\.\s+([^\t]+)\t' -and $Matches[2] -eq $Target) {
            return [int]$Matches[1]
        }
    }
    return 0
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

New-Item (Split-Path -Parent $Report) -ItemType Directory -Force | Out-Null
$results = [System.Collections.Generic.List[object]]::new()
$paragraph = 0
Get-Content $Corpus -Encoding UTF8 | Where-Object { $_ -and -not $_.StartsWith('#') } | ForEach-Object {
    ++$paragraph
    $clauses = $_ -split '[，。！？；：、“”‘’（）]+'
    foreach ($clause in $clauses) {
        foreach ($chunk in Convert-ToChunks $clause) {
            if ([string]::IsNullOrWhiteSpace($chunk.Pinyin)) { continue }
            $query = $chunk.Pinyin -replace "'", ''
            $xiaoheInput = Convert-PinyinToXiaohe $chunk.Pinyin
            $fullRank = Find-CandidateRank 'full' $query $chunk.Target
            $xiaoheRank = Find-CandidateRank 'flypy' $xiaoheInput $chunk.Target
            $results.Add([pscustomobject]@{
                Paragraph = $paragraph
                Target = $chunk.Target
                FullPinyin = $query
                FullRank = $fullRank
                Xiaohe = $xiaoheInput
                XiaoheRank = $xiaoheRank
                BothHit = ($fullRank -gt 0 -and $xiaoheRank -gt 0)
            })
        }
    }
}

$results | Select-Object Paragraph,Target,FullPinyin,FullRank,Xiaohe,XiaoheRank,BothHit |
    Export-Csv -LiteralPath $Report -Delimiter "`t" -NoTypeInformation -Encoding UTF8
$total = $results.Count
$fullHits = @($results | Where-Object FullRank -gt 0).Count
$xiaoheHits = @($results | Where-Object XiaoheRank -gt 0).Count
$bothHits = @($results | Where-Object BothHit).Count
$fullRate = if ($total -eq 0) { 0 } else { [Math]::Round(100.0 * $fullHits / $total, 2) }
$xiaoheRate = if ($total -eq 0) { 0 } else { [Math]::Round(100.0 * $xiaoheHits / $total, 2) }
$bothRate = if ($total -eq 0) { 0 } else { [Math]::Round(100.0 * $bothHits / $total, 2) }
Write-Host "Full pinyin: $fullHits/$total chunks hit Top 10 ($fullRate%)." -ForegroundColor Green
Write-Host "Xiaohe: $xiaoheHits/$total chunks hit Top 10 ($xiaoheRate%)." -ForegroundColor Green
Write-Host "Both schemes: $bothHits/$total chunks hit Top 10 ($bothRate%)." -ForegroundColor Green
Write-Host "Report: $Report" -ForegroundColor Cyan
if ($bothRate -lt $RequiredHitRate) {
    throw "Both-scheme corpus hit rate $bothRate% is below required $RequiredHitRate%."
}
