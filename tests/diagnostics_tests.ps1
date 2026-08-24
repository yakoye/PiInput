param([Parameter(Mandatory = $true)][string]$DiagnosticsExe)
$ErrorActionPreference = "Stop"
$raw = & $DiagnosticsExe --status --json 2>&1
if ($LASTEXITCODE -ne 0) { throw "Diagnostics failed: $($raw -join ' ')" }
$status = ($raw -join "`n") | ConvertFrom-Json
foreach ($name in @(
    "protocol_version", "profile_registered", "profile_enabled", "profile_active",
    "shim_path", "shim_exists", "shim_sha256", "host_path", "host_exists",
    "machine_shim_path", "machine_shim_exists", "machine_shim_matches_user",
    "machine_shim_sha256",
    "host_sha256", "lexicon_path", "lexicon_exists", "lexicon_sha256",
    "host_connected", "host_health", "legacy_module_scan")) {
    if ($status.PSObject.Properties.Name -notcontains $name) {
        throw "Diagnostics JSON is missing $name"
    }
}
if ($status.protocol_version -ne 3) { throw "Unexpected protocol version" }
if ($status.legacy_module_scan -ne "not_performed") {
    throw "Diagnostics must not pretend to know legacy loaded-module state"
}
foreach ($pair in @(
    @("shim_exists", "shim_sha256"),
    @("machine_shim_exists", "machine_shim_sha256"),
    @("host_exists", "host_sha256"),
    @("lexicon_exists", "lexicon_sha256"))) {
    if ($status.($pair[0]) -and [string]$status.($pair[1]) -notmatch '^[0-9a-f]{64}$') {
        throw "Diagnostics returned an invalid $($pair[1]) for an existing file"
    }
}
Write-Host "PiInput diagnostics tests passed."
