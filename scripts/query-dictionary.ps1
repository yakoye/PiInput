param(
    [string]$Value = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$RepoRoot = Split-Path -Parent $PSScriptRoot
$Cli = Join-Path $RepoRoot "dist/windows-x64/bin/piinput-cli.exe"
if (-not (Test-Path -LiteralPath $Cli -PathType Leaf)) {
    . (Join-Path $PSScriptRoot "windows/resolve-installed-dev.ps1")
    $Layout = Resolve-PiInputInstalledDev
    $Cli = Join-Path $Layout.Bin "piinput-cli.exe"
}
if (-not (Test-Path -LiteralPath $Cli -PathType Leaf)) {
    throw "Cannot find piinput-cli.exe. Build or install PiInput first."
}
$Lexicon = Join-Path $env:LOCALAPPDATA "PiInput/UserData/lexicons/piinput-imported.lex"
if (-not (Test-Path -LiteralPath $Lexicon -PathType Leaf)) {
    throw "Cannot find the installed PiInput dictionary: $Lexicon"
}
if ([string]::IsNullOrWhiteSpace($Value)) {
    $Value = Read-Host "请输入要查询的中文词语，或带单引号分隔的标准拼音"
}
if ([string]::IsNullOrWhiteSpace($Value)) {
    throw "查询内容不能为空。"
}
if ($Value -match "^[A-Za-z']+$") {
    & $Cli --lexicon $Lexicon --lookup-pinyin $Value.ToLowerInvariant() --top 50
} else {
    & $Cli --lexicon $Lexicon --lookup-word $Value --top 50
}
if ($LASTEXITCODE -eq 2) {
    Write-Host "词库中没有找到匹配项。" -ForegroundColor Yellow
    exit 0
}
if ($LASTEXITCODE -ne 0) {
    throw "词库查询失败，退出代码：$LASTEXITCODE"
}
