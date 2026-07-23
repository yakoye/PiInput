param(
    [string]$DictionaryRoot = "",
    [switch]$Force,
    [switch]$IncludeEnglish
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
    if ($source.id -eq "wordfreq-en-25000") {
        if (-not $IncludeEnglish) {
            Write-Host "Skipping optional English source (use -IncludeEnglish to download)." -ForegroundColor DarkGray
            continue
        }
        $destination = Join-Path $SourcesRoot $source.id
        New-Item -ItemType Directory -Force $destination | Out-Null
        $cachedFile = Join-Path $destination $source.file
        $downloadFile = "$cachedFile.download"
        try {
            Remove-Item -LiteralPath $downloadFile -Force -ErrorAction SilentlyContinue
            Write-Host "Downloading pinned $($source.id)..." -ForegroundColor Cyan
            Invoke-WebRequest -UseBasicParsing -Uri $source.rawFile -OutFile $downloadFile
            $actualHash = (Get-FileHash -LiteralPath $downloadFile -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($actualHash -ne $source.sha256.ToLowerInvariant()) {
                throw "SHA-256 mismatch for $($source.id): $actualHash"
            }
            Move-Item -LiteralPath $downloadFile -Destination $cachedFile -Force
        } catch {
            Remove-Item -LiteralPath $downloadFile -Force -ErrorAction SilentlyContinue
            Write-Warning "Updating $($source.id) failed; existing cache was preserved. $($_.Exception.Message)"
        }
        continue
    }
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
