param([switch]$SkipRegistryCheck)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$Root = $PSScriptRoot
$Bin = Join-Path $Root "dist/windows-x64/bin"
$Data = Join-Path $Root "data/base_lexicon.tsv"
$Cli = Join-Path $Bin "liteime-cli.exe"
$Profile = Join-Path $Bin "liteime-profile.exe"
$Dll = Join-Path $Bin "LiteImeTSF.dll"

foreach ($path in @($Cli, $Profile, $Dll, $Data)) {
    if (-not (Test-Path $path)) {
        throw "Missing verification input: $path"
    }
}

$FullResult = (& $Cli --lexicon $Data --schema full --query jisuanji --top 5 | Out-String)
if ($LASTEXITCODE -ne 0 -or -not $FullResult.Contains("计算机")) {
    throw "Full-pinyin base lexicon verification failed.`n$FullResult"
}
$FlypyResult = (& $Cli --lexicon $Data --schema flypy --query jisrji --top 5 | Out-String)
if ($LASTEXITCODE -ne 0 -or -not $FlypyResult.Contains("计算机")) {
    throw "Flypy base lexicon verification failed.`n$FlypyResult"
}

if (-not $SkipRegistryCheck) {
    $RegistryPath = "HKCU:\Software\Classes\CLSID\{84E21A77-3A42-4D7B-93B8-BCDF818FC414}\InprocServer32"
    if (-not (Test-Path $RegistryPath)) {
        throw "LiteIME COM registration was not found. Run .\setup-dev.cmd or .\repair-registration.ps1."
    }
    $RegisteredDll = (Get-ItemProperty $RegistryPath).'(default)'
    Write-Host "Registered DLL: $RegisteredDll" -ForegroundColor Cyan
}


if (-not $SkipRegistryCheck) {
    $StatusResult = (& $Profile --status 2>&1 | Out-String)
    if ($LASTEXITCODE -ne 0 -or -not $StatusResult.Contains("registered=yes") -or -not $StatusResult.Contains("enabled=yes")) {
        throw "LiteIME TSF profile status verification failed.`n$StatusResult"
    }
    Write-Host $StatusResult.Trim() -ForegroundColor Cyan
}

Write-Host "Full pinyin query passed: jisuanji -> 计算机" -ForegroundColor Green
Write-Host "Flypy query passed: jisrji -> 计算机" -ForegroundColor Green
Write-Host "Automated Windows verification passed." -ForegroundColor Green
Write-Host "Manual TSF check:" -ForegroundColor Cyan
Write-Host "  1. Close and reopen Notepad."
Write-Host "  2. Press Win+Space and select LiteIME 中文输入法（开发版）."
Write-Host "  3. Type jisrji (default developer schema is Flypy)."
Write-Host "  4. Press Space; 计算机 should be committed."
exit 0
