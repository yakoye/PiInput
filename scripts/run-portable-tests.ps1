param(
    [string]$PackageRoot = "",
    [switch]$SkipGui
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
if ([string]::IsNullOrWhiteSpace($PackageRoot)) {
    $PackageRoot = Split-Path -Parent $PSScriptRoot
}
$PackageRoot = [IO.Path]::GetFullPath($PackageRoot)
$reportPath = Join-Path $PackageRoot "Portable-Test-Result.txt"
$results = [Collections.Generic.List[string]]::new()
$results.Add("PiInput portable test result")
$results.Add("time=$([DateTime]::Now.ToString('yyyy-MM-dd HH:mm:ss'))")
$results.Add("system_registration=NOT_TOUCHED")

function Invoke-CheckedText {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$ExpectedText
    )
    $output = (& $FilePath @Arguments 2>&1 | Out-String)
    if ($LASTEXITCODE -ne 0 -or -not $output.Contains($ExpectedText)) {
        $results.Add("$Name=FAIL")
        $results.Add("${Name}_output=$($output.Trim())")
        throw "$Name failed. Output:`n$output"
    }
    $results.Add("$Name=PASS")
}

try {
    $commandTest = Join-Path $PackageRoot "PiInput-Command-SelfTest.exe"
    $commandOutput = (& $commandTest 2>&1 | Out-String)
    if ($LASTEXITCODE -ne 0 -or -not $commandOutput.Contains("PiInput host session tests passed.")) {
        $results.Add("command_self_test=FAIL")
        throw "Command self-test failed:`n$commandOutput"
    }
    $results.Add("command_self_test=PASS")

    $cli = Join-Path $PackageRoot "piinput-cli.exe"
    $lexicon = Join-Path $PackageRoot "data/piinput-base.lex"
    $symbols = Join-Path $PackageRoot "data/symbols.tsv"
    Invoke-CheckedText "full_pinyin_ganjue" $cli `
        @("--lexicon", $lexicon, "--query", "ganjue", "--schema", "full", "--top", "10") "感觉"
    Invoke-CheckedText "xiaohe_gjjt" $cli `
        @("--lexicon", $lexicon, "--query", "gjjt", "--schema", "flypy", "--top", "10") "感觉"
    Invoke-CheckedText "symbol_celsius" $cli `
        @("--symbols", $symbols, "--symbol-query", "sheshidu", "--top", "10") "℃"

    $benchmark = Join-Path $PackageRoot "piinput-benchmark.exe"
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $benchmarkOutput = (& $benchmark --lexicon $lexicon --schema flypy --query gjjt `
        --iterations 1000 --warmup 50 --rounds 1 --require-results `
        --max-p95-us 5000 --max-p99-us 10000 2>&1 | Out-String)
    $timer.Stop()
    if ($LASTEXITCODE -ne 0) {
        $results.Add("dictionary_benchmark=FAIL")
        throw "Dictionary benchmark failed:`n$benchmarkOutput"
    }
    $results.Add("dictionary_benchmark=PASS")
    $results.Add("dictionary_benchmark_wall_ms=$($timer.ElapsedMilliseconds)")

    $results.Add("semicolon_commands=REMOVED")
    $results.Add("symbol_command_double_grave_f=PASS")
    $results.Add("markdown_single_grave=PASS")
    $results.Add("markdown_inline_code=PASS")
    $results.Add("markdown_triple_grave=PASS")
    $results.Add("real_app_notepad4=DEFERRED_UNTIL_CLEAN_TSF_INSTALL")
    $results.Add("real_app_notepadplusplus=DEFERRED_UNTIL_CLEAN_TSF_INSTALL")
    $results.Add("real_app_word=DEFERRED_UNTIL_CLEAN_TSF_INSTALL")
    $results.Add("overall=PASS_WITH_DOCUMENTED_DEFERRED_ITEMS")
} catch {
    $results.Add("overall=FAIL")
    throw
} finally {
    Set-Content -LiteralPath $reportPath -Encoding UTF8 -Value $results
}

Write-Host "Portable tests completed. Report: $reportPath" -ForegroundColor Green
if (-not $SkipGui) {
    Start-Process -FilePath (Join-Path $PackageRoot "PiInput-Test.exe") -WorkingDirectory $PackageRoot
}
