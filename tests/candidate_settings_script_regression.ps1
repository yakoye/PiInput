$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) {
        throw $Message
    }
}

$RepositoryRoot = Split-Path -Parent $PSScriptRoot
$TemporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) (
    "piinput-candidate-settings-" + [guid]::NewGuid().ToString("N"))
$PreviousLocalAppData = $env:LOCALAPPDATA

try {
    $env:LOCALAPPDATA = $TemporaryRoot
    $UserData = Join-Path $TemporaryRoot "PiInput/UserData"
    $SettingsPath = Join-Path $UserData "settings.ini"
    New-Item $UserData -ItemType Directory -Force | Out-Null
    @"
[general]
schema=full

[pinyin]
uv_compatibility=false

[candidates]
items_per_row=5
visible_rows=1
max_items=9
horizontal=false

[english]
enabled=false
"@ | Set-Content $SettingsPath -Encoding ASCII

    & (Join-Path $RepositoryRoot "set-candidate-page-size.ps1") `
        -ItemsPerRow 9 -VisibleRows 5 -MaxItems 45

    $Content = Get-Content $SettingsPath -Raw
    Assert-True ($Content -match '(?m)^\[general\]\r?$' -and $Content -match '(?m)^schema=full\r?$') `
        "The candidate settings tool did not preserve [general]."
    Assert-True ($Content -match '(?m)^\[pinyin\]\r?$' -and $Content -match '(?m)^uv_compatibility=false\r?$') `
        "The candidate settings tool did not preserve [pinyin]."
    Assert-True ($Content -match '(?m)^\[english\]\r?$' -and $Content -match '(?m)^enabled=false\r?$') `
        "The candidate settings tool did not preserve [english]."
    Assert-True ($Content -match '(?m)^\[candidates\]\r?$') `
        "The candidate settings tool did not write [candidates]."
    Assert-True ($Content -match '(?m)^items_per_row=9\r?$' -and
        $Content -match '(?m)^visible_rows=5\r?$' -and
        $Content -match '(?m)^max_items=45\r?$') `
        "The candidate settings tool did not write the requested grid."
    Assert-True ($Content -match '(?m)^horizontal=false\r?$') `
        "The candidate settings tool removed an unrelated candidate setting."
    Assert-True ($Content -notmatch 'single_syllable_page_size|phrase_page_size') `
        "The candidate settings tool retained obsolete paging keys."

    $Rejected = $false
    try {
        & (Join-Path $RepositoryRoot "set-candidate-page-size.ps1") `
            -ItemsPerRow 9 -VisibleRows 5 -MaxItems 44
    } catch {
        $Rejected = $true
    }
    Assert-True $Rejected "The candidate settings tool accepted max_items smaller than one screen."
} finally {
    $env:LOCALAPPDATA = $PreviousLocalAppData
    if (Test-Path -LiteralPath $TemporaryRoot) {
        Remove-Item -LiteralPath $TemporaryRoot -Recurse -Force
    }
}

Write-Host "Candidate settings script regression passed."
