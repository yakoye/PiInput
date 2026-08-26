param(
    [Parameter(Mandatory = $true)][string]$Cli,
    [Parameter(Mandatory = $true)][string]$SourceDir
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$Lexicon = Join-Path $SourceDir "tests/data/dictionary_builder/lookup.tsv"
$ByWord = (& $Cli --lexicon $Lexicon --lookup-word "黄河入海流" 2>&1) | Out-String
if ($LASTEXITCODE -ne 0 -or $ByWord -notmatch "huang'he'ru'hai'liu") {
    throw "--lookup-word did not return the exact dictionary entry: $ByWord"
}
$ByPinyin = (& $Cli --lexicon $Lexicon --lookup-pinyin "huang'he'ru'hai'liu" --top 10 2>&1) | Out-String
if ($LASTEXITCODE -ne 0 -or $ByPinyin -notmatch "(?m)^1\. 黄河入海流\s") {
    throw "--lookup-pinyin did not return the exact pinyin entry: $ByPinyin"
}
