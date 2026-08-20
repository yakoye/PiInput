# Long-form typing latency gate.
#
# Replays the three real-world test texts through a live resident Host one
# physical key at a time, including the '=' row paging and digit keys a user
# presses when the wanted word is not candidate 1. Latency is the gate; the
# candidate hit rate is reported for tracking but is not asserted, because
# candidate quality is a separate open workstream.
param(
    [Parameter(Mandatory = $true)][string]$HostExe,
    [Parameter(Mandatory = $true)][string]$ClientExe,
    [Parameter(Mandatory = $true)][string]$ScriptDirectory,
    [string]$LexiconPath = "",
    [string]$DataDir = "",
    [int]$MaxKeyP95Us = 4000,
    [int]$MaxKeyUs = 15000
)

$ErrorActionPreference = "Stop"
$server = $null
$previousInstance = $env:PIINPUT_HOST_INSTANCE
$previousPackageData = $env:PIINPUT_PACKAGE_DATA_DIR
$previousUserData = $env:PIINPUT_USER_DATA_DIR
$fixture = $null
$env:PIINPUT_HOST_INSTANCE = "ctest_typing_$PID"
try {
    foreach ($required in @($HostExe, $ClientExe)) {
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
            throw "Missing executable: $required"
        }
    }
    if ([string]::IsNullOrWhiteSpace($LexiconPath) -or
        -not (Test-Path -LiteralPath $LexiconPath -PathType Leaf)) {
        Write-Host "SKIP: real lexicon is unavailable, long-form typing gate not run."
        exit 77
    }

    $fixture = Join-Path ([IO.Path]::GetTempPath()) ("piinput-typing-" + [guid]::NewGuid().ToString("N"))
    $fixtureData = Join-Path $fixture "data"
    $fixtureUser = Join-Path $fixture "user"
    New-Item -ItemType Directory -Path $fixtureData, $fixtureUser -Force | Out-Null
    Copy-Item -LiteralPath $LexiconPath -Destination (Join-Path $fixtureData "piinput-base.lex")
    if (-not [string]::IsNullOrWhiteSpace($DataDir)) {
        foreach ($name in @("symbols.tsv", "english_lexicon.tsv", "english_supplement.tsv", "english_completion_preferences.tsv")) {
            $source = Join-Path $DataDir $name
            if (Test-Path -LiteralPath $source -PathType Leaf) {
                Copy-Item -LiteralPath $source -Destination (Join-Path $fixtureData $name)
            }
        }
    }
    $env:PIINPUT_PACKAGE_DATA_DIR = $fixtureData
    $env:PIINPUT_USER_DATA_DIR = $fixtureUser

    $server = Start-Process -FilePath $HostExe -ArgumentList "--serve" -PassThru -WindowStyle Hidden
    $ready = $false
    for ($attempt = 0; $attempt -lt 80; $attempt++) {
        & $HostExe --health *> $null
        if ($LASTEXITCODE -eq 0) { $ready = $true; break }
        Start-Sleep -Milliseconds 50
    }
    if (-not $ready) { throw "Host did not become healthy" }

    $totals = @{ Keys = 0; Clauses = 0; First = 0; Row = 0; Paging = 0; Missing = 0 }
    $worstP95 = 0
    $worstKey = 0
    foreach ($group in 1, 2, 3) {
        $scriptPath = Join-Path $ScriptDirectory "text-$group.keys.tsv"
        if (-not (Test-Path -LiteralPath $scriptPath -PathType Leaf)) {
            throw "Missing typing script: $scriptPath"
        }
        $output = @(& $ClientExe --type-script $scriptPath 2>&1 | ForEach-Object { $_.ToString() })
        if ($LASTEXITCODE -ne 0) {
            throw "Typing replay failed for text $group : $($output -join ' ')"
        }
        $values = @{}
        foreach ($line in $output) {
            if ($line -match '^([a-z_0-9]+)=([0-9]+)$') { $values[$Matches[1]] = [int]$Matches[2] }
        }
        foreach ($name in @('keys', 'key_p95_us', 'key_max_us', 'clauses')) {
            if (-not $values.ContainsKey($name)) {
                throw "Typing replay did not report $name for text $group"
            }
        }
        $totals.Keys += $values['keys']
        $totals.Clauses += $values['clauses']
        $totals.First += $values['target_at_first']
        $totals.Row += $values['target_on_first_row']
        $totals.Paging += $values['target_needed_row_paging']
        $totals.Missing += $values['target_not_in_page']
        if ($values['key_p95_us'] -gt $worstP95) { $worstP95 = $values['key_p95_us'] }
        if ($values['key_max_us'] -gt $worstKey) { $worstKey = $values['key_max_us'] }
        Write-Host ("text {0}: keys={1} p50={2}us p95={3}us max={4}us boundary_p95={5}us" -f `
            $group, $values['keys'], $values['key_p50_us'], $values['key_p95_us'], `
            $values['key_max_us'], $values['boundary_p95_us'])
    }

    $reachable = $totals.First + $totals.Row + $totals.Paging
    Write-Host ("totals: clauses={0} keys={1} first={2} first_row={3} reachable={4} missing={5}" -f `
        $totals.Clauses, $totals.Keys, $totals.First, ($totals.First + $totals.Row), `
        $reachable, $totals.Missing)

    if ($totals.Keys -lt 1500) {
        throw "Long-form typing replay covered only $($totals.Keys) keys"
    }
    if ($worstP95 -ge $MaxKeyP95Us) {
        throw "Long-form typing per-key P95 ${worstP95}us reached the ${MaxKeyP95Us}us limit"
    }
    if ($worstKey -ge $MaxKeyUs) {
        throw "Long-form typing slowest key ${worstKey}us reached the ${MaxKeyUs}us limit"
    }

    & $HostExe --drain *> $null
    if (-not $server.WaitForExit(5000)) { throw "Host did not exit after drain" }
    Write-Host "PiInput long-form typing latency gate passed."
}
finally {
    if ($null -ne $server -and -not $server.HasExited) {
        Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
    }
    $env:PIINPUT_HOST_INSTANCE = $previousInstance
    $env:PIINPUT_PACKAGE_DATA_DIR = $previousPackageData
    $env:PIINPUT_USER_DATA_DIR = $previousUserData
    if ($null -ne $fixture -and (Test-Path -LiteralPath $fixture)) {
        Remove-Item -LiteralPath $fixture -Recurse -Force -ErrorAction SilentlyContinue
    }
}
