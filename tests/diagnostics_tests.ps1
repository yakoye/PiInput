param([Parameter(Mandatory = $true)][string]$DiagnosticsExe)
$ErrorActionPreference = "Stop"
$raw = & $DiagnosticsExe --status --json 2>&1
if ($LASTEXITCODE -ne 0) { throw "Diagnostics failed: $($raw -join ' ')" }
$status = ($raw -join "`n") | ConvertFrom-Json
foreach ($name in @(
    "protocol_version", "profile_registered", "profile_enabled", "profile_active",
    "shim_path", "shim_exists", "shim_sha256", "host_path", "host_exists",
    "host_connected", "host_health", "legacy_module_scan")) {
    if ($status.PSObject.Properties.Name -notcontains $name) {
        throw "Diagnostics JSON is missing $name"
    }
}
if ($status.protocol_version -ne 3) { throw "Unexpected protocol version" }
if ($status.legacy_module_scan -ne "not_performed") {
    throw "Diagnostics must not pretend to know legacy loaded-module state"
}
Write-Host "PiInput diagnostics tests passed."
