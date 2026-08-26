param(
    [Parameter(Mandatory = $true)][string]$Builder,
    [Parameter(Mandatory = $true)][string]$SourceDir
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$TempRoot = Join-Path ([IO.Path]::GetTempPath()) ("PiInput-dictionary-builder-test-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $TempRoot | Out-Null
try {
    $Output = Join-Path $TempRoot "combined.tsv"
    $Report = Join-Path $TempRoot "report.tsv"
    $Pronunciations = Join-Path $SourceDir "tests/data/dictionary_builder/pronunciations.tsv"
    $Terms = Join-Path $SourceDir "tests/data/dictionary_builder/thuocl_poem.txt"
    & $Builder --output $Output --report $Report `
        --source tsv $Pronunciations 1 `
        --source thuocl $Terms 1500
    if ($LASTEXITCODE -ne 0) {
        throw "dictionary builder returned $LASTEXITCODE"
    }
    $Text = Get-Content -Raw -Encoding UTF8 $Output
    if ($Text -notmatch "黄河入海流`t+huang'he'ru'hai'liu`t+") {
        throw "derived poem entry is missing from builder output"
    }
    if ($Text -notmatch "更上一层楼`t+geng'shang'yi'ceng'lou`t+") {
        throw "exact phrase pronunciation is missing from builder output"
    }
    $ReportText = Get-Content -Raw -Encoding UTF8 $Report
    foreach ($Expected in @("exact_phrase`t1", "character_derived`t1", "unresolved`t1")) {
        if ($ReportText -notmatch [regex]::Escape($Expected)) {
            throw "dictionary report is missing: $Expected"
        }
    }
} finally {
    if (Test-Path -LiteralPath $TempRoot) {
        Remove-Item -LiteralPath $TempRoot -Recurse -Force
    }
}
