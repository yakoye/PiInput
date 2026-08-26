param([switch]$SkipRegistryCheck)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
# Two levels up: this script moved from the repository root into
# scripts/dev, and $Root still means the repository root.
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Bin = Join-Path $Root "dist/windows-x64/bin"
$Data = Join-Path $Root "data/base_lexicon.tsv"
$Cli = Join-Path $Bin "piinput-cli.exe"
$Profile = Join-Path $Bin "piinput-profile.exe"
$Dll = Join-Path $Bin "PiInputTSF.dll"
$InstalledLexicon = Join-Path $env:LOCALAPPDATA "PiInput/UserData/lexicons/piinput-imported.lex"
$CandidateSettings = Join-Path $env:LOCALAPPDATA "PiInput/UserData/settings.ini"

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
if (Test-Path $InstalledLexicon) {
    foreach ($case in @(
        @("flypy", "jpiu", "接触"),
        @("flypy", "cihv", "词汇"),
        @("full", "jiechu", "接触"),
        @("full", "cihui", "词汇")
    )) {
        $result = (& $Cli --lexicon $InstalledLexicon --schema $case[0] --query $case[1] --top 6 | Out-String)
        if ($LASTEXITCODE -ne 0 -or $result -notmatch ("(?m)^1\. " + [regex]::Escape($case[2]) + "\s")) {
            throw "Installed dictionary ranking failed: $($case[1]) must rank $($case[2]) first.`n$result"
        }
    }
}
if (-not (Test-Path $CandidateSettings)) {
    throw "Candidate settings are missing: $CandidateSettings"
}

if (-not $SkipRegistryCheck) {
    $RegistryPaths = @(
        "HKCU:\Software\Classes\CLSID\{D73AABA7-BE3E-4E53-8DE2-652D352743F3}\InprocServer32",
        "HKLM:\Software\Classes\CLSID\{D73AABA7-BE3E-4E53-8DE2-652D352743F3}\InprocServer32"
    )
    $RegistryPath = $RegistryPaths | Where-Object { Test-Path $_ } | Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($RegistryPath)) {
        throw "PiInput COM registration was not found. Run .\setup-dev.cmd or .\repair-registration.ps1."
    }
    $RegisteredDll = (Get-ItemProperty $RegistryPath).'(default)'
    Write-Host "Registered DLL: $RegisteredDll" -ForegroundColor Cyan
    if (-not (Test-Path $RegisteredDll)) {
        throw "Registered PiInput DLL does not exist: $RegisteredDll"
    }
    $BuiltHash = (Get-FileHash $Dll -Algorithm SHA256).Hash
    $RegisteredHash = (Get-FileHash $RegisteredDll -Algorithm SHA256).Hash
    if ($BuiltHash -ne $RegisteredHash) {
        throw "The registered TSF DLL is stale. Run .\setup-dev.cmd to install the current build."
    }
}


if (-not $SkipRegistryCheck) {
    $StatusResult = (& $Profile --status 2>&1 | Out-String)
    if ($LASTEXITCODE -ne 0 -or -not $StatusResult.Contains("registered=yes") -or -not $StatusResult.Contains("enabled=yes")) {
        throw "PiInput TSF profile status verification failed.`n$StatusResult"
    }
    Write-Host $StatusResult.Trim() -ForegroundColor Cyan
}

Write-Host "Full pinyin query passed: jisuanji -> 计算机" -ForegroundColor Green
Write-Host "Flypy query passed: jisrji -> 计算机" -ForegroundColor Green
Write-Host "Registered TSF DLL matches the current Release build." -ForegroundColor Green
Write-Host "Automated Windows verification passed." -ForegroundColor Green
Write-Host "Manual TSF check:" -ForegroundColor Cyan
Write-Host "  1. Close and reopen Notepad."
Write-Host "  2. Press Win+Space and select PiInput 中文输入法."
Write-Host "  3. Type jisrji (default developer schema is Flypy)."
Write-Host "  4. Press Space; 计算机 should be committed."
exit 0
