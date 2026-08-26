param(
    [Parameter(Mandatory = $true)][string]$SourceDir,
    [Parameter(Mandatory = $true)][string]$BuildDir,
    [Parameter(Mandatory = $true)][string]$DistDir
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$packager = Join-Path $SourceDir "scripts/windows/package-portable-test.ps1"
if (-not (Test-Path -LiteralPath $packager -PathType Leaf)) {
    throw "Missing portable test packager: $packager"
}

$fixture = Join-Path ([IO.Path]::GetTempPath()) ("piinput-portable-package-" + [Guid]::NewGuid().ToString("N"))
$artifacts = Join-Path $fixture "artifacts"
$expanded = Join-Path $fixture "expanded"
try {
    & $packager -Configuration Release -BuildDir $BuildDir -DistDir $DistDir `
        -ArtifactsDir $artifacts -VersionLabel "regression"

    $zip = Join-Path $artifacts "PiInput-regression-portable-test-windows-x64.zip"
    if (-not (Test-Path -LiteralPath $zip -PathType Leaf)) {
        throw "Portable package was not created: $zip"
    }
    Expand-Archive -LiteralPath $zip -DestinationPath $expanded -Force
    $root = Join-Path $expanded "PiInput-regression-portable-test-windows-x64"

    foreach ($required in @(
        "PiInput-Test.exe",
        "PiInput-Command-SelfTest.exe",
        "piinput-cli.exe",
        "piinput-benchmark.exe",
        "Run-Portable-Tests.cmd",
        "scripts/run-portable-tests.ps1",
        "data/piinput-base.lex",
        "data/symbols.tsv",
        "data/english_lexicon.tsv",
        "PORTABLE_TEST_GUIDE.md",
        "PORTABLE_TEST_RESULTS_2026-08-15.md"
    )) {
        $path = Join-Path $root $required
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Portable package is missing: $required"
        }
    }

    foreach ($forbidden in @(
        "PiInput-Install.exe",
        "PiInput-Uninstall.exe",
        "PiInputTSF.dll",
        "PiInputHost.exe",
        "piinput-profile.exe"
    )) {
        if (Test-Path -LiteralPath (Join-Path $root $forbidden)) {
            throw "Portable package must not contain system integration payload: $forbidden"
        }
    }

    $runner = Join-Path $root "scripts/run-portable-tests.ps1"
    & $runner -PackageRoot $root -SkipGui
    $report = Join-Path $root "Portable-Test-Result.txt"
    if (-not (Test-Path -LiteralPath $report -PathType Leaf)) {
        throw "Portable package did not write its result report."
    }
    $reportText = Get-Content -Raw -LiteralPath $report
    foreach ($marker in @(
        "command_self_test=PASS",
        "full_pinyin_ganjue=PASS",
        "xiaohe_gjjt=PASS",
        "symbol_celsius=PASS",
        "system_registration=NOT_TOUCHED"
    )) {
        if (-not $reportText.Contains($marker)) {
            throw "Portable report is missing result marker: $marker"
        }
    }
} finally {
    Remove-Item -LiteralPath $fixture -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "Portable package regression passed."
