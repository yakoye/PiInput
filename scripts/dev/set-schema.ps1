param(
    [ValidateSet("full", "flypy", "natural", "mspy", "abc")]
    [Parameter(Mandatory = $true)]
    [string]$Schema
)

$ErrorActionPreference = "Stop"
$Tool = Join-Path $env:LOCALAPPDATA "PiInput/Dev/bin/piinput-profile.exe"
if (-not (Test-Path $Tool)) {
    throw "PiInput is not installed. Run .\setup-dev.cmd first."
}
& $Tool --schema $Schema
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
Write-Host "The new schema will be used the next time the PiInput profile is activated in an application." -ForegroundColor Cyan
exit 0
