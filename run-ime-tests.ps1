param([string]$DictionaryRoot = "")

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$Root = $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($DictionaryRoot)) {
    $DictionaryRoot = Join-Path (Split-Path -Parent $Root) "dicts"
}
& (Join-Path $Root "build.ps1") -Configuration Release -TestDataDir $DictionaryRoot
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$Tests = Join-Path $Root "build/windows-x64/Release/piinput-core-tests.exe"
$Benchmark = Join-Path $Root "dist/windows-x64/bin/piinput-benchmark.exe"
$Lexicon = Join-Path $DictionaryRoot "cache/piinput-base.lex"
if (Test-Path $Lexicon) {
    & $Tests --lexicon $Lexicon
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $Benchmark --lexicon $Lexicon --schema full --query wo --iterations 10000 --warmup 1000 --max-p95-us 2000 --max-p99-us 5000
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} else {
    Write-Host "External dictionary cache not found; CTest coverage still ran." -ForegroundColor Yellow
}
Write-Host "PiInput full-pinyin, Xiaohe and latency tests passed." -ForegroundColor Green
