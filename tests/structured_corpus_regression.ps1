param(
    [Parameter(Mandatory = $true)][string]$Runner,
    [Parameter(Mandatory = $true)][string]$Lexicon,
    [Parameter(Mandatory = $true)][string]$SourceDir,
    [Parameter(Mandatory = $true)][string]$TempRoot
)

$ErrorActionPreference = "Stop"
$corpus = Join-Path $SourceDir "tests/corpus/v0.2.0/tests"
$caseFile = Join-Path $TempRoot "piinput-structured-corpus-$PID.tsv"

function Read-Cases([string]$Name) {
    return @(Get-Content -Raw -LiteralPath (Join-Path $corpus $Name) | ConvertFrom-Json)
}

function Schema-Id([string]$Scheme) {
    switch ($Scheme) {
        "full_pinyin" { return "full" }
        "xiaohe" { return "flypy" }
        default { throw "Unsupported corpus scheme: $Scheme" }
    }
}

function Clean-Field([object]$Value) {
    $text = if ($null -eq $Value) { "" } else { [string]$Value }
    if ($text.Contains("`t") -or $text.Contains("`r") -or $text.Contains("`n")) {
        throw "Structured corpus fields may not contain tabs or newlines"
    }
    return $text
}

$rows = [System.Collections.Generic.List[string]]::new()
$rows.Add("# id`tmode`tschema`tinput`ttarget`tmax_rank")

$language = Read-Cases "language_model_test_cases.json"
if ($language.Count -ne 84) { throw "Expected 84 language-model cases" }
foreach ($item in $language) {
    $fields = @($item.id, "results", (Schema-Id $item.scheme),
        $item.input_sequence, "", 90) | ForEach-Object { Clean-Field $_ }
    $rows.Add($fields -join "`t")
}

$professional = Read-Cases "professional_vocabulary_test_cases.json"
if ($professional.Count -ne 59) { throw "Expected 59 professional-vocabulary cases" }
$knownMissing = @(Get-Content -Raw -LiteralPath (
    Join-Path $SourceDir "tests/data/known_missing_professional_targets.json") |
    ConvertFrom-Json | ForEach-Object { foreach ($entry in $_) { $entry } })
$knownMissingIds = @($knownMissing | ForEach-Object { [string]$_.id })
if ($knownMissingIds.Count -ne 13 -or
    @($knownMissingIds | Sort-Object -Unique).Count -ne $knownMissingIds.Count) {
    throw "Expected 13 unique known-missing professional target IDs"
}
foreach ($item in $professional) {
    $mode = if ($knownMissingIds -contains [string]$item.id) { "known_missing" } else { "rank" }
    $fields = @($item.id, $mode, (Schema-Id $item.scheme),
        $item.input_sequence, $item.target_text,
        $item.expected.recommended_max_rank) | ForEach-Object { Clean-Field $_ }
    $rows.Add($fields -join "`t")
}
$professionalIds = @($professional | ForEach-Object { [string]$_.id })
foreach ($id in $knownMissingIds) {
    if ($professionalIds -notcontains $id) { throw "Unknown professional baseline ID: $id" }
}

$correction = Read-Cases "correction_test_cases.json"
if ($correction.Count -ne 160) { throw "Expected 160 correction cases" }
foreach ($item in $correction) {
    # Correction is currently disabled by product policy. Execute every mutated
    # input against the ordinary decoder to guarantee stable raw/no-crash behavior.
    $fields = @($item.id, "smoke", (Schema-Id $item.scheme),
        $item.mutated_input, "", 90) | ForEach-Object { Clean-Field $_ }
    $rows.Add($fields -join "`t")
}

$fuzzy = Read-Cases "fuzzy_pinyin_test_cases.json"
$fuzzyOff = @($fuzzy | Where-Object { -not $_.fuzzy_enabled })
if ($fuzzy.Count -ne 20 -or $fuzzyOff.Count -ne 10) {
    throw "Expected 20 fuzzy-pinyin cases with 10 disabled-policy cases"
}
foreach ($item in $fuzzyOff) {
    $fields = @($item.id, "smoke", (Schema-Id $item.scheme),
        $item.test_input, "", 90) | ForEach-Object { Clean-Field $_ }
    $rows.Add($fields -join "`t")
}

if ($rows.Count -ne 314) { throw "Expected header plus 313 executable corpus cases" }

[System.IO.Directory]::CreateDirectory($TempRoot) | Out-Null
[System.IO.File]::WriteAllLines(
    $caseFile, $rows, [System.Text.UTF8Encoding]::new($false))
try {
    & $Runner $Lexicon $caseFile
    if ($LASTEXITCODE -ne 0) { throw "Structured corpus runner failed with $LASTEXITCODE" }
} finally {
    Remove-Item -LiteralPath $caseFile -Force -ErrorAction SilentlyContinue
}
