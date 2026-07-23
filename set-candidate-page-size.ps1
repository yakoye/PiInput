param(
    [ValidateRange(5, 9)]
    [int]$ItemsPerRow = 6,
    [ValidateRange(1, 5)]
    [int]$VisibleRows = 3,
    [ValidateRange(9, 180)]
    [int]$MaxItems = 90
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if ($MaxItems -lt $ItemsPerRow * $VisibleRows) {
    throw "MaxItems must be at least ItemsPerRow * VisibleRows."
}

$SettingsDirectory = Join-Path $env:LOCALAPPDATA "PiInput/UserData"
$SettingsPath = Join-Path $SettingsDirectory "settings.ini"
New-Item $SettingsDirectory -ItemType Directory -Force | Out-Null

$Lines = if (Test-Path $SettingsPath) {
    @(Get-Content $SettingsPath -Encoding UTF8)
} else {
    @()
}
$Output = @()
$InCandidates = $false
$FoundCandidates = $false

foreach ($Line in $Lines) {
    if ($Line -match '^\s*\[([^\]]+)\]\s*$') {
        if ($InCandidates) {
            $Output += "items_per_row=$ItemsPerRow"
            $Output += "visible_rows=$VisibleRows"
            $Output += "max_items=$MaxItems"
        }
        $InCandidates = $Matches[1] -ieq "candidates"
        $FoundCandidates = $FoundCandidates -or $InCandidates
        $Output += $Line
        continue
    }
    if ($Line -match '^\s*(single_syllable_page_size|phrase_page_size)\s*=') {
        continue
    }
    if ($InCandidates -and
        $Line -match '^\s*(items_per_row|visible_rows|max_items)\s*=') {
        continue
    }
    $Output += $Line
}

if ($InCandidates) {
    $Output += "items_per_row=$ItemsPerRow"
    $Output += "visible_rows=$VisibleRows"
    $Output += "max_items=$MaxItems"
} elseif (-not $FoundCandidates) {
    if ($Output.Count -gt 0 -and $Output[-1] -ne "") {
        $Output += ""
    }
    $Output += "[candidates]"
    $Output += "items_per_row=$ItemsPerRow"
    $Output += "visible_rows=$VisibleRows"
    $Output += "max_items=$MaxItems"
}

$Output | Set-Content $SettingsPath -Encoding UTF8
Write-Host "PiInput candidate layout saved: columns=$ItemsPerRow, rows=$VisibleRows, max=$MaxItems" `
    -ForegroundColor Green
Write-Host "The new layout will apply at the next composition boundary." -ForegroundColor Cyan
