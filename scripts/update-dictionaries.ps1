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
$SourcesRoot = Join-Path $DictionaryRoot "sources"
New-Item -ItemType Directory -Force $SourcesRoot | Out-Null
foreach ($name in @("scel", "user", "generated", "cache")) {
    New-Item -ItemType Directory -Force (Join-Path $DictionaryRoot $name) | Out-Null
}

$sources = Get-Content -Raw (Join-Path $RepoRoot "dictionary_sources.json") | ConvertFrom-Json
foreach ($source in $sources.sources) {
    $destination = Join-Path $SourcesRoot $source.id
    if (-not (Test-Path (Join-Path $destination ".git"))) {
        Write-Host "Downloading $($source.id)..." -ForegroundColor Cyan
        & git clone --depth 1 $source.repository $destination
        if ($LASTEXITCODE -ne 0) { throw "Downloading $($source.id) failed." }
    } else {
        Write-Host "Updating $($source.id)..." -ForegroundColor Cyan
        & git -C $destination fetch --depth 1 origin
        if ($LASTEXITCODE -ne 0) { throw "Fetching $($source.id) failed; existing cache was preserved." }
        $branch = (& git -C $destination symbolic-ref --short refs/remotes/origin/HEAD 2>$null) -replace '^origin/', ''
        if ([string]::IsNullOrWhiteSpace($branch)) { $branch = "master" }
        & git -C $destination reset --hard "origin/$branch"
        if ($LASTEXITCODE -ne 0) { throw "Updating $($source.id) failed; existing cache was preserved." }
    }
}

& (Join-Path $PSScriptRoot "build-dictionaries.ps1") -DictionaryRoot $DictionaryRoot -Force:$Force
exit $LASTEXITCODE
