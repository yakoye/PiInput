param(
    [string]$DictionaryRoot = "",
    [string]$RimeIceRoot = "",
    [switch]$Force,
    [switch]$IncludeExtendedFallback,
    [switch]$SkipInstall
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot "native-command.ps1")
$RepoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($DictionaryRoot)) {
    $DictionaryRoot = Join-Path (Split-Path -Parent $RepoRoot) "dicts"
}
if ([string]::IsNullOrWhiteSpace($RimeIceRoot)) {
    $localRimeIce = Join-Path $DictionaryRoot "rime-ice-full"
    $downloadedRimeIce = Join-Path $DictionaryRoot "sources/rime-ice-full"
    $RimeIceRoot = if (Test-Path $localRimeIce) { $localRimeIce } else { $downloadedRimeIce }
}
$RimeIceMaster = Join-Path $RimeIceRoot "rime_ice.dict.yaml"
$RimeIceTables = @(
    $RimeIceMaster
    (Join-Path $RimeIceRoot "cn_dicts/8105.dict.yaml")
    (Join-Path $RimeIceRoot "cn_dicts/base.dict.yaml")
    (Join-Path $RimeIceRoot "cn_dicts/ext.dict.yaml")
    (Join-Path $RimeIceRoot "cn_dicts/tencent.dict.yaml")
    (Join-Path $RimeIceRoot "cn_dicts/others.dict.yaml")
)
foreach ($rimeFile in $RimeIceTables) {
    if (-not (Test-Path -LiteralPath $rimeFile -PathType Leaf)) {
        throw "Required Rime Ice dictionary file is missing: $rimeFile"
    }
}
$Bin = Join-Path $RepoRoot "dist/windows-x64/bin"
$Builder = Join-Path $Bin "piinput-dictionary-builder.exe"
$Compiler = Join-Path $Bin "piinput-lexicon-compiler.exe"
$Cli = Join-Path $Bin "piinput-cli.exe"
$Benchmark = Join-Path $Bin "piinput-benchmark.exe"
$Coverage = Join-Path $RepoRoot "build/windows-x64/Release/piinput-character-coverage-tests.exe"
Write-Host "Building current PiInput dictionary tools..." -ForegroundColor Cyan
& (Join-Path $RepoRoot "build.ps1") -Configuration Release -SkipTests
if ($LASTEXITCODE -ne 0) { throw "Building dictionary tools failed." }

$Generated = Join-Path $DictionaryRoot "generated"
$Cache = Join-Path $DictionaryRoot "cache"
New-Item -ItemType Directory -Force $Generated, $Cache | Out-Null
$Manifest = Join-Path $Cache "dictionary-build-manifest.json"
function Install-ValidatedLexicon {
    param([string]$SourceLexicon)
    $installedDir = Join-Path $env:LOCALAPPDATA "PiInput/UserData/lexicons"
    New-Item -ItemType Directory -Force $installedDir | Out-Null
    $installed = Join-Path $installedDir "piinput-imported.lex"
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
$sourceState["profile"] = "rime-ice-default-v1"
$sourceState["builder"] = [ordered]@{
    executable = (Get-FileHash $Builder -Algorithm SHA256).Hash
    script = (Get-FileHash $PSCommandPath -Algorithm SHA256).Hash
}
$sourceState["rime_ice"] = @($RimeIceTables | ForEach-Object {
    [ordered]@{ path = $_.Substring($RimeIceRoot.Length); sha256 = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash }
})
$stateJson = $sourceState | ConvertTo-Json -Depth 5 -Compress
$FinalLex = Join-Path $Cache "piinput-base.lex"
if (-not $Force -and (Test-Path $Manifest) -and (Test-Path $FinalLex)) {
    if ((Get-Content -Raw $Manifest) -eq $stateJson) {
        Write-Host "Dictionary sources are unchanged; reusing $FinalLex" -ForegroundColor Green
        if (-not $SkipInstall) {
            Install-ValidatedLexicon $FinalLex
        }
        exit 0
    }
}

$staging = Join-Path ([IO.Path]::GetTempPath()) ("PiInput-dictionaries-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force $staging | Out-Null
try {
    $arguments = [System.Collections.Generic.List[string]]::new()
    $arguments.Add("--output"); $arguments.Add((Join-Path $staging "combined.tsv"))
    $arguments.Add("--rime-dictionary"); $arguments.Add($RimeIceMaster); $arguments.Add("1")
    $arguments.Add("--rime-report"); $arguments.Add((Join-Path $staging "rime-ice-import-report.tsv"))
    & $Builder @arguments
    if ($LASTEXITCODE -ne 0) { throw "Dictionary normalization failed." }
    $stagedTsv = Join-Path $staging "combined.tsv"
    $stagedLex = Join-Path $staging "piinput-base.lex"
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
        @("full", "kaiwu", "开悟"),
        @("flypy", "kdwu", "开悟"),
        @("full", "sihua", "丝滑"),
        @("flypy", "sihx", "丝滑"),
        @("full", "biankuang", "边框"),
        @("full", "houxuankuang", "候选框"),
        @("full", "huangheruhailiu", "黄河入海流"),
        @("flypy", "hlheruhdlq", "黄河入海流")
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
        @("flypy", "cihv", "词汇"),
        @("full", "huangheruhailiu", "黄河入海流"),
        @("flypy", "hlheruhdlq", "黄河入海流")
    )) {
        $rankingOutput = (& $Cli --lexicon $stagedLex --schema $ranking[0] --query $ranking[1] --top 6 2>&1) | Out-String
        if ($rankingOutput -notmatch ("(?m)^1\. " + [regex]::Escape($ranking[2]) + "\s")) {
            throw "Dictionary ranking verification failed: $($ranking[1]) must rank $($ranking[2]) first."
        }
    }
    $CommonCharacters = Join-Path $DictionaryRoot "tests/3500常用汉字.txt"
    $GeneralCharacters = Join-Path $DictionaryRoot "tests/7000通用汉字.txt"
    if ((Test-Path $CommonCharacters) -and (Test-Path $GeneralCharacters)) {
        & $Coverage $stagedTsv $stagedLex $CommonCharacters $GeneralCharacters (Join-Path $RepoRoot "tests/data/xiaohe_legal_codes.tsv")
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "Rime Ice default 8105 table does not cover every optional 3500/7000 fixture character; keeping the Rime Ice source unchanged."
        }
    }
    & $Benchmark --lexicon $stagedLex --schema full --query wo --iterations 10000 --warmup 1000 --max-p95-us 2000 --max-p99-us 5000
    if ($LASTEXITCODE -ne 0) { throw "Dictionary latency verification failed." }

    $GeneratedTsv = Join-Path $Generated "piinput-combined.tsv"
    $RimeReportPath = Join-Path $Generated "rime-ice-import-report.tsv"
    Copy-Item $stagedTsv ($GeneratedTsv + ".new") -Force
    Move-Item ($GeneratedTsv + ".new") $GeneratedTsv -Force
    Copy-Item (Join-Path $staging "rime-ice-import-report.tsv") ($RimeReportPath + ".new") -Force
    Move-Item ($RimeReportPath + ".new") $RimeReportPath -Force
    Move-Item $stagedLex ($FinalLex + ".new") -Force
    Move-Item ($FinalLex + ".new") $FinalLex -Force
    if (-not $SkipInstall) {
        Install-ValidatedLexicon $FinalLex
    }
    [IO.File]::WriteAllText($Manifest, $stateJson, [Text.UTF8Encoding]::new($false))
    Write-Host "Rime Ice report: $RimeReportPath" -ForegroundColor Cyan
    Write-Host "Dictionary built atomically: $FinalLex" -ForegroundColor Green
} finally {
    if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
}
