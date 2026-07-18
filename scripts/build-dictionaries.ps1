param(
    [string]$DictionaryRoot = "",
    [switch]$Force
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$RepoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($DictionaryRoot)) {
    $DictionaryRoot = Join-Path (Split-Path -Parent $RepoRoot) "dicts"
}
$Bin = Join-Path $RepoRoot "dist/windows-x64/bin"
$Builder = Join-Path $Bin "liteime-dictionary-builder.exe"
$Compiler = Join-Path $Bin "liteime-lexicon-compiler.exe"
$Converter = Join-Path $Bin "liteime-scel-converter.exe"
$Cli = Join-Path $Bin "liteime-cli.exe"
$Benchmark = Join-Path $Bin "liteime-benchmark.exe"
Write-Host "Building current LiteIME dictionary tools..." -ForegroundColor Cyan
& (Join-Path $RepoRoot "build.ps1") -Configuration Release
if ($LASTEXITCODE -ne 0) { throw "Building dictionary tools failed." }

$Sources = Join-Path $DictionaryRoot "sources"
$Generated = Join-Path $DictionaryRoot "generated"
$Cache = Join-Path $DictionaryRoot "cache"
New-Item -ItemType Directory -Force $Generated, $Cache | Out-Null
$Manifest = Join-Path $Cache "dictionary-build-manifest.json"
function Install-ValidatedLexicon {
    param([string]$SourceLexicon)
    $installedDir = Join-Path $env:LOCALAPPDATA "LiteIME/UserData/lexicons"
    New-Item -ItemType Directory -Force $installedDir | Out-Null
    $installed = Join-Path $installedDir "liteime-imported.lex"
    $needsCopy = -not (Test-Path $installed)
    if (-not $needsCopy) {
        $needsCopy = (Get-FileHash $SourceLexicon -Algorithm SHA256).Hash -ne (Get-FileHash $installed -Algorithm SHA256).Hash
    }
    if ($needsCopy) {
        Copy-Item $SourceLexicon ($installed + ".new") -Force
        Move-Item ($installed + ".new") $installed -Force
    }
    Write-Host "Installed dictionary ready: $installed" -ForegroundColor Green
}
$sourceState = [ordered]@{}
$sourceState["builder"] = [ordered]@{
    executable = (Get-FileHash $Builder -Algorithm SHA256).Hash
    script = (Get-FileHash $PSCommandPath -Algorithm SHA256).Hash
}
foreach ($source in @("pinyin-data", "phrase-pinyin-data", "rime-pinyin-simp", "THUOCL")) {
    $path = Join-Path $Sources $source
    if (Test-Path (Join-Path $path ".git")) {
        $sourceState[$source] = (& git -C $path rev-parse HEAD).Trim()
    }
}
$scelFiles = @(Get-ChildItem $DictionaryRoot -Recurse -File -Filter *.scel -ErrorAction SilentlyContinue)
$localFiles = @(
    $scelFiles
    Get-ChildItem (Join-Path $DictionaryRoot "user") -Recurse -File -Include *.tsv -ErrorAction SilentlyContinue
)
$sourceState["local"] = @($localFiles | Sort-Object FullName | ForEach-Object {
    [ordered]@{ path = $_.FullName.Substring($DictionaryRoot.Length); sha256 = (Get-FileHash $_.FullName -Algorithm SHA256).Hash }
})
$stateJson = $sourceState | ConvertTo-Json -Depth 5 -Compress
$FinalLex = Join-Path $Cache "liteime-base.lex"
if (-not $Force -and (Test-Path $Manifest) -and (Test-Path $FinalLex)) {
    if ((Get-Content -Raw $Manifest) -eq $stateJson) {
        Write-Host "Dictionary sources are unchanged; reusing $FinalLex" -ForegroundColor Green
        Install-ValidatedLexicon $FinalLex
        exit 0
    }
}

$staging = Join-Path ([IO.Path]::GetTempPath()) ("LiteIME-dictionaries-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force $staging | Out-Null
try {
    $arguments = [System.Collections.Generic.List[string]]::new()
    $arguments.Add("--output"); $arguments.Add((Join-Path $staging "combined.tsv"))
    $arguments.Add("--source"); $arguments.Add("tsv"); $arguments.Add((Join-Path $RepoRoot "data/base_lexicon.tsv")); $arguments.Add("50000")
    $arguments.Add("--source"); $arguments.Add("pinyin-data"); $arguments.Add((Join-Path $Sources "pinyin-data/kMandarin_8105.txt")); $arguments.Add("1000")
    # phrase-pinyin-data supplies pronunciation, not input frequency. Keep its
    # fallback deliberately low so it cannot outrank Rime's measured weights.
    $arguments.Add("--source"); $arguments.Add("phrase-pinyin-data"); $arguments.Add((Join-Path $Sources "phrase-pinyin-data/large_pinyin.txt")); $arguments.Add("1")
    $arguments.Add("--source"); $arguments.Add("rime"); $arguments.Add((Join-Path $Sources "rime-pinyin-simp/pinyin_simp.dict.yaml")); $arguments.Add("10000")

    foreach ($file in Get-ChildItem (Join-Path $DictionaryRoot "user") -Recurse -File -Filter *.tsv -ErrorAction SilentlyContinue) {
        $arguments.Add("--source"); $arguments.Add("tsv"); $arguments.Add($file.FullName); $arguments.Add("60000")
    }
    foreach ($file in $scelFiles) {
        $converted = Join-Path $staging ($file.BaseName + ".tsv")
        & $Converter --input $file.FullName --output $converted --format tsv
        if ($LASTEXITCODE -ne 0) { throw "SCEL conversion failed: $($file.FullName)" }
        $arguments.Add("--source"); $arguments.Add("tsv"); $arguments.Add($converted); $arguments.Add("20000")
    }
    & $Builder @arguments
    if ($LASTEXITCODE -ne 0) { throw "Dictionary normalization failed." }
    $stagedTsv = Join-Path $staging "combined.tsv"
    $stagedLex = Join-Path $staging "liteime-base.lex"
    & $Compiler --input $stagedTsv --output $stagedLex
    if ($LASTEXITCODE -ne 0) { throw "Binary dictionary compilation failed." }

    $cases = @(
        @("flypy", "jpiu", "接触"),
        @("flypy", "cihv", "词汇"),
        @("flypy", "gjjt", "感觉"),
        @("flypy", "xmzd", "现在"),
        @("flypy", "vsgo", "中国"),
        @("full", "wo", "我"),
        @("full", "ganjue", "感觉"),
        @("full", "jiechu", "接触"),
        @("full", "cihui", "词汇"),
        @("full", "wojintianxiawuyaoquchaoshimaidianshuiguo", "我今天下午要去超市买点水果"),
        @("full", "gujiankaifaxuyaoshuxidicengjicunqipeizhihelianluzhuangtaiji", "固件开发需要熟悉底层寄存器配置和链路状态机")
    )
    foreach ($case in $cases) {
        $output = (& $Cli --lexicon $stagedLex --schema $case[0] --query $case[1] --top 10 2>&1) | Out-String
        if ($LASTEXITCODE -ne 0 -or $output -notmatch [regex]::Escape($case[2])) {
            throw "Dictionary verification failed: $($case[0]) $($case[1]) must contain $($case[2])."
        }
    }
    $feeling = (& $Cli --lexicon $stagedLex --schema flypy --query gjjt --top 5 2>&1) | Out-String
    if ($feeling -notmatch '(?m)^1\. 感觉\s') {
        throw "Dictionary ranking verification failed: gjjt must rank 感觉 first."
    }
    foreach ($ranking in @(
        @("flypy", "jpiu", "接触"),
        @("flypy", "cihv", "词汇")
    )) {
        $rankingOutput = (& $Cli --lexicon $stagedLex --schema $ranking[0] --query $ranking[1] --top 6 2>&1) | Out-String
        if ($rankingOutput -notmatch ("(?m)^1\. " + [regex]::Escape($ranking[2]) + "\s")) {
            throw "Dictionary ranking verification failed: $($ranking[1]) must rank $($ranking[2]) first."
        }
    }
    & $Benchmark --lexicon $stagedLex --schema full --query wo --iterations 10000 --warmup 1000 --max-p95-us 2000 --max-p99-us 5000
    if ($LASTEXITCODE -ne 0) { throw "Dictionary latency verification failed." }

    $GeneratedTsv = Join-Path $Generated "liteime-combined.tsv"
    Copy-Item $stagedTsv ($GeneratedTsv + ".new") -Force
    Move-Item ($GeneratedTsv + ".new") $GeneratedTsv -Force
    Move-Item $stagedLex ($FinalLex + ".new") -Force
    Move-Item ($FinalLex + ".new") $FinalLex -Force
    Install-ValidatedLexicon $FinalLex
    [IO.File]::WriteAllText($Manifest, $stateJson, [Text.UTF8Encoding]::new($false))
    Write-Host "Dictionary built atomically: $FinalLex" -ForegroundColor Green
} finally {
    if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
}
