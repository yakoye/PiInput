param([switch]$SkipBuild)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$Root = $PSScriptRoot
$Source = Join-Path $Root "dist/windows-x64"
$Destination = Join-Path $env:LOCALAPPDATA "PiInput/Dev"
$SourceDll = Join-Path $Source "bin/PiInputTSF.dll"
$InstalledDll = Join-Path $Destination "bin/PiInputTSF.dll"

if (-not $SkipBuild) {
    & (Join-Path $Root "build.ps1") -Configuration Release
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
if (-not (Test-Path $SourceDll)) { throw "Current Release DLL is missing: $SourceDll" }

$blockers = @(Get-Process -ErrorAction SilentlyContinue | ForEach-Object {
    try {
        foreach ($module in $_.Modules) {
            if ($module.FileName -eq $InstalledDll -and $_.ProcessName -notin @("ctfmon", "explorer")) {
                [pscustomobject]@{ Process = $_.ProcessName; Id = $_.Id; Window = $_.MainWindowTitle }
                break
            }
        }
    } catch {}
})
if ($blockers.Count -gt 0) {
    Write-Host "Close these applications, then run this file again:" -ForegroundColor Yellow
    $blockers | Format-Table -AutoSize
    throw "The installed TSF DLL is still in use. No application was closed automatically."
}

Get-Process ctfmon, explorer -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
try {
    New-Item $Destination -ItemType Directory -Force | Out-Null
    Copy-Item (Join-Path $Source "bin") $Destination -Recurse -Force
    Copy-Item (Join-Path $Source "data") $Destination -Recurse -Force
} finally {
    Start-Process (Join-Path $env:SystemRoot "explorer.exe")
    Start-Process (Join-Path $env:SystemRoot "System32/ctfmon.exe")
}

$sourceHash = (Get-FileHash $SourceDll -Algorithm SHA256).Hash
$installedHash = (Get-FileHash $InstalledDll -Algorithm SHA256).Hash
if ($sourceHash -ne $installedHash) { throw "Installed DLL hash does not match the current build." }
Write-Host "PiInput current Release build was installed without changing TSF registration." -ForegroundColor Green
Write-Host "Reopen the target application and switch to PiInput once." -ForegroundColor Cyan
