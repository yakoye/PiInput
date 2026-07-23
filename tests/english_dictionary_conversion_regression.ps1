$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepoRoot = Split-Path -Parent $PSScriptRoot
$Converter = Join-Path $RepoRoot "scripts/convert-english-wordfreq.ps1"
$TempRoot = Join-Path ([System.IO.Path]::GetTempPath()) (
    "piinput-english-conversion-" + [Guid]::NewGuid().ToString("N"))
$InputJson = Join-Path $TempRoot "wordfreq.json"
$OutputTsv = Join-Path $TempRoot "generated/english_lexicon.tsv"
$RuntimeTsv = Join-Path $TempRoot "localappdata/PiInput/UserData/english_downloaded.tsv"

try {
    New-Item -ItemType Directory -Force $TempRoot | Out-Null
    @'
[
  ["the", -2.8],
  ["Apple", -3.0],
  ["two words", -4.0],
  ["123", -5.0],
  ["alpha", -6.0],
  ["alpha", -7.0],
  ["café", -8.0]
]
'@ | Set-Content -LiteralPath $InputJson -Encoding UTF8

    & $Converter -InputJson $InputJson -OutputTsv $OutputTsv -RuntimeTsv $RuntimeTsv
    $actual = @(Get-Content -LiteralPath $OutputTsv -Encoding UTF8)
    $expected = @(
        "the`t3`t2",
        "Apple`t2`t2",
        "alpha`t1`t2"
    )
    if (($actual -join "`n") -cne ($expected -join "`n")) {
        throw "Unexpected converted English TSV: $($actual -join ' | ')"
    }
    if (Test-Path -LiteralPath "$OutputTsv.tmp") {
        throw "Successful conversion left a temporary file"
    }
    if ((Get-Content -Raw -LiteralPath $RuntimeTsv -Encoding UTF8) -cne
        (Get-Content -Raw -LiteralPath $OutputTsv -Encoding UTF8)) {
        throw "Runtime downloaded TSV does not match the generated TSV"
    }
    if (Test-Path -LiteralPath "$RuntimeTsv.tmp") {
        throw "Successful runtime publish left a temporary file"
    }

    $beforeFailure = Get-Content -Raw -LiteralPath $OutputTsv -Encoding UTF8
    $runtimeBeforeFailure = Get-Content -Raw -LiteralPath $RuntimeTsv -Encoding UTF8
    "{ damaged json" | Set-Content -LiteralPath $InputJson -Encoding UTF8
    $failed = $false
    try {
        & $Converter -InputJson $InputJson -OutputTsv $OutputTsv -RuntimeTsv $RuntimeTsv
    } catch {
        $failed = $true
    }
    if (-not $failed) {
        throw "Damaged JSON conversion did not fail"
    }
    $afterFailure = Get-Content -Raw -LiteralPath $OutputTsv -Encoding UTF8
    if ($afterFailure -cne $beforeFailure) {
        throw "Failed conversion replaced the existing generated English TSV"
    }
    if ((Get-Content -Raw -LiteralPath $RuntimeTsv -Encoding UTF8) -cne
        $runtimeBeforeFailure) {
        throw "Failed conversion replaced the installed downloaded English TSV"
    }
    if (Test-Path -LiteralPath "$OutputTsv.tmp") {
        throw "Failed conversion left a temporary file"
    }
    if (Test-Path -LiteralPath "$RuntimeTsv.tmp") {
        throw "Failed conversion left a runtime temporary file"
    }
} finally {
    if (Test-Path -LiteralPath $TempRoot) {
        Remove-Item -LiteralPath $TempRoot -Recurse -Force
    }
}

Write-Host "English dictionary conversion regression passed"
