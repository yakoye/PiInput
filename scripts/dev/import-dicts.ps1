param(
    [string]$DictionaryDir = "",
    [string]$DestinationDir = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

# Two levels up: this script moved from the repository root into
# scripts/dev, and $Root still means the repository root.
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if ([string]::IsNullOrWhiteSpace($DictionaryDir)) {
    $DictionaryDir = Join-Path (Split-Path -Parent $Root) "dicts"
}
if ([string]::IsNullOrWhiteSpace($DestinationDir)) {
    $DestinationDir = Join-Path $env:LOCALAPPDATA "PiInput/UserData/lexicons"
}

$Bin = Join-Path $Root "dist/windows-x64/bin"
$Converter = Join-Path $Bin "piinput-scel-converter.exe"
$Compiler = Join-Path $Bin "piinput-lexicon-compiler.exe"
$GeneratedBaseTsv = Join-Path $DictionaryDir "generated/piinput-combined.tsv"
$BaseTsv = if (Test-Path $GeneratedBaseTsv) { $GeneratedBaseTsv } else { Join-Path $Root "data/base_lexicon.tsv" }
if (-not (Test-Path $Converter) -or -not (Test-Path $Compiler)) {
    throw "Build PiInput first with .\build.ps1"
}
if (-not (Test-Path $BaseTsv)) {
    throw "Built-in base lexicon is missing: $BaseTsv"
}

New-Item $DestinationDir -ItemType Directory -Force | Out-Null
$TemporaryDir = Join-Path ([IO.Path]::GetTempPath()) ("PiInput-import-" + [Guid]::NewGuid().ToString("N"))
New-Item $TemporaryDir -ItemType Directory -Force | Out-Null
$ConvertedTsvFiles = [System.Collections.Generic.List[string]]::new()

try {
    # The built-in starter dictionary is always present. This prevents a professional-only
    # SCEL collection from making ordinary words such as “计算机” or “输入法” disappear.
    $BaseLex = Join-Path $DestinationDir "piinput-base.lex"
    & $Compiler --input $BaseTsv --output $BaseLex
    if ($LASTEXITCODE -ne 0) {
        throw "Built-in base lexicon compilation failed: $BaseTsv"
    }
    $ConvertedTsvFiles.Add($BaseTsv)
    Write-Host "Built-in base dictionary: $BaseLex" -ForegroundColor Green

    $ScelFiles = @()
    if (Test-Path $DictionaryDir) {
        $ScelFiles = @(Get-ChildItem $DictionaryDir -Filter *.scel -File -Recurse | Sort-Object FullName)
    }

    foreach ($file in $ScelFiles) {
        $safeName = [IO.Path]::GetFileNameWithoutExtension($file.Name)
        $tsv = Join-Path $TemporaryDir "$safeName.tsv"
        $lex = Join-Path $DestinationDir "$safeName.lex"

        & $Converter --input $file.FullName --output $tsv --format tsv
        if ($LASTEXITCODE -ne 0) {
            throw "SCEL conversion failed: $($file.FullName)"
        }
        & $Compiler --input $tsv --output $lex
        if ($LASTEXITCODE -ne 0) {
            throw "Lexicon compilation failed: $tsv"
        }
        $ConvertedTsvFiles.Add($tsv)
        Write-Host "Imported dictionary: $lex" -ForegroundColor Green
    }

    # Build one deterministic combined dictionary used by the preview and TSF text service.
    # The compiler performs word+pinyin de-duplication and keeps the highest weight.
    $CombinedTsv = Join-Path $TemporaryDir "piinput-imported.tsv"
    $Utf8NoBom = [Text.UTF8Encoding]::new($false)
    $Writer = [IO.StreamWriter]::new($CombinedTsv, $false, $Utf8NoBom)
    try {
        $Writer.WriteLine("# PiInput combined base and imported lexicon")
        $Writer.WriteLine((@("word", "pinyin", "weight") -join "`t"))
        foreach ($tsv in $ConvertedTsvFiles) {
            foreach ($line in [IO.File]::ReadLines($tsv, $Utf8NoBom)) {
                if ([string]::IsNullOrWhiteSpace($line) -or $line.StartsWith("#") -or $line.StartsWith("word`t")) {
                    continue
                }
                $Writer.WriteLine($line)
            }
        }
    } finally {
        $Writer.Dispose()
    }

    $CombinedLex = Join-Path $DestinationDir "piinput-imported.lex"
    & $Compiler --input $CombinedTsv --output $CombinedLex
    if ($LASTEXITCODE -ne 0) {
        throw "Combined lexicon compilation failed: $CombinedTsv"
    }

    Write-Host "Combined dictionary: $CombinedLex" -ForegroundColor Cyan
    if ($ScelFiles.Count -eq 0) {
        Write-Host "No SCEL files were found. PiInput will use the built-in starter dictionary." -ForegroundColor Yellow
    } else {
        Write-Host "Imported $($ScelFiles.Count) SCEL dictionaries plus the built-in starter dictionary." -ForegroundColor Green
    }
} finally {
    if (Test-Path $TemporaryDir) {
        Remove-Item $TemporaryDir -Recurse -Force
    }
}
