param(
    [ValidateRange(1, 9)]
    [int]$SingleSyllable = 9,
    [ValidateRange(1, 9)]
    [int]$Phrase = 6
)

$ErrorActionPreference = "Stop"
$SettingsDirectory = Join-Path $env:LOCALAPPDATA "LiteIME/UserData"
$SettingsPath = Join-Path $SettingsDirectory "settings.ini"
New-Item $SettingsDirectory -ItemType Directory -Force | Out-Null
$preserved = @()
if (Test-Path $SettingsPath) {
    $preserved = @(Get-Content $SettingsPath | Where-Object {
        $_ -notmatch '^(single_syllable_page_size|phrase_page_size)='
    })
}
@(
    $preserved
    "single_syllable_page_size=$SingleSyllable"
    "phrase_page_size=$Phrase"
) | Set-Content $SettingsPath -Encoding ASCII
Write-Host "LiteIME candidate page sizes saved: single=$SingleSyllable, phrase=$Phrase" -ForegroundColor Green
Write-Host "Switch away from LiteIME and back once to reload the settings." -ForegroundColor Cyan
