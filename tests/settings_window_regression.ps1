param(
    [Parameter(Mandatory = $true)][string]$SettingsExe
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if (-not (Test-Path -LiteralPath $SettingsExe -PathType Leaf)) {
    throw "PiInput-Settings.exe is missing: $SettingsExe"
}

# The settings window is the only way to change settings without hand-editing
# settings.ini, so "double-click does nothing" makes the feature unreachable.
# Launch it and require a real window, twice in a row: the second run also
# covers the single-instance path, which must focus the existing window rather
# than exit silently.
$fixtureDir = Join-Path ([IO.Path]::GetTempPath()) ("piinput-settings-" + [Guid]::NewGuid().ToString("N"))
New-Item $fixtureDir -ItemType Directory -Force | Out-Null
$settingsPath = Join-Path $fixtureDir "settings.ini"
Set-Content -LiteralPath $settingsPath -Encoding UTF8 -Value @"
[general]
schema=flypy
"@

# A settings window left over from an earlier run owns the single-instance
# mutex, and a second launch correctly hands that window the focus and exits --
# which looks exactly like the failure this test is here to catch.
Get-Process -Name "PiInput-Settings" -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 300

$processes = @()
try {
    $first = Start-Process -FilePath $SettingsExe -ArgumentList "--settings", $settingsPath -PassThru
    $processes += $first
    $window = [IntPtr]::Zero
    for ($attempt = 0; $attempt -lt 60 -and $window -eq [IntPtr]::Zero; $attempt++) {
        Start-Sleep -Milliseconds 100
        if ($first.HasExited) {
            throw "Settings window exited immediately with code $($first.ExitCode) instead of showing a window"
        }
        $first.Refresh()
        $window = $first.MainWindowHandle
    }
    if ($window -eq [IntPtr]::Zero) {
        throw "Settings window never appeared"
    }
    Write-Host "settings_window=yes"
}
finally {
    foreach ($process in $processes) {
        if ($null -ne $process -and -not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        }
    }
    Remove-Item -LiteralPath $fixtureDir -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "PiInput settings window regression passed."
