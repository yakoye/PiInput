param(
    [Parameter(Mandatory = $true)][string]$HostExe,
    [Parameter(Mandatory = $true)][string]$ClientExe,
    [Parameter(Mandatory = $true)][string]$LexiconPath,
    [Parameter(Mandatory = $true)][string]$DataDir,
    [double]$DurationHours = 8.0,
    [int]$SampleSeconds = 30,
    [string]$OutputDirectory = "",
    [int64]$MaxPrivateGrowthBytes = 67108864,
    [int64]$MaxWorkingSetGrowthBytes = 100663296,
    [int]$MaxHandleGrowth = 64,
    [double]$MaxPrivateSlopeBytesPerHour = 8388608,
    [double]$MaxHandleSlopePerHour = 4.0,
    [double]$MinSlopeEvaluationHours = 1.0
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
if ($DurationHours -le 0 -or $SampleSeconds -lt 1) { throw "DurationHours and SampleSeconds must be positive." }
foreach ($required in @($HostExe, $ClientExe, $LexiconPath)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing soak-test input: $required" }
}
if (-not (Test-Path -LiteralPath $DataDir -PathType Container)) { throw "Missing data directory: $DataDir" }
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path (Split-Path -Parent $PSScriptRoot) "artifacts/soak"
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$samplesPath = Join-Path $OutputDirectory "host-resource-samples.csv"
$summaryPath = Join-Path $OutputDirectory "summary.json"

function Get-SlopePerHour([object[]]$Rows, [string]$Property) {
    if ($Rows.Count -lt 2) { return 0.0 }
    $meanX = ($Rows | Measure-Object ElapsedHours -Average).Average
    $meanY = ($Rows | Measure-Object $Property -Average).Average
    $numerator = 0.0
    $denominator = 0.0
    foreach ($row in $Rows) {
        $dx = [double]$row.ElapsedHours - $meanX
        $numerator += $dx * ([double]$row.$Property - $meanY)
        $denominator += $dx * $dx
    }
    if ($denominator -eq 0) { return 0.0 }
    return $numerator / $denominator
}

function Invoke-SoakClient([string[]]$Arguments, [string]$Label) {
    $output = @(& $ClientExe @Arguments 2>&1)
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "$Label failed with exit code $exitCode`: $($output -join ' ')"
    }
}

function Write-SoakSample([object]$Row, [string]$Path) {
    $csv = @($Row | ConvertTo-Csv -NoTypeInformation)
    $needsHeader = -not (Test-Path -LiteralPath $Path) -or
        (Get-Item -LiteralPath $Path).Length -eq 0
    $lines = if ($needsHeader) { $csv } else { @($csv[-1]) }
    $payload = ($lines -join "`r`n") + "`r`n"
    $encoding = [Text.UTF8Encoding]::new($false)
    foreach ($attempt in 1..20) {
        $stream = $null
        $writer = $null
        try {
            $stream = [IO.FileStream]::new(
                $Path, [IO.FileMode]::Append, [IO.FileAccess]::Write,
                [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
            $writer = [IO.StreamWriter]::new($stream, $encoding)
            $writer.Write($payload)
            $writer.Flush()
            return
        } catch [IO.IOException] {
            if ($attempt -eq 20) { throw }
            Start-Sleep -Milliseconds 100
        } finally {
            if ($null -ne $writer) { $writer.Dispose() }
            elseif ($null -ne $stream) { $stream.Dispose() }
        }
    }
}

$fixture = Join-Path ([IO.Path]::GetTempPath()) ("PiInput-soak-" + [guid]::NewGuid().ToString("N"))
$server = $null
$previousInstance = $env:PIINPUT_HOST_INSTANCE
$previousPackageData = $env:PIINPUT_PACKAGE_DATA_DIR
$previousUserData = $env:PIINPUT_USER_DATA_DIR
$rows = [Collections.Generic.List[object]]::new()
$failure = $null
$startedUtc = [DateTime]::UtcNow
$healthText = ""
$buildId = ""
try {
    $fixtureData = Join-Path $fixture "data"
    $fixtureUser = Join-Path $fixture "user"
    New-Item -ItemType Directory -Path $fixtureData, $fixtureUser -Force | Out-Null
    Copy-Item -LiteralPath $LexiconPath -Destination (Join-Path $fixtureData "piinput-base.lex")
    foreach ($name in @("symbols.tsv", "english_lexicon.tsv", "english_supplement.tsv", "english_completion_preferences.tsv")) {
        $source = Join-Path $DataDir $name
        if (Test-Path -LiteralPath $source -PathType Leaf) { Copy-Item -LiteralPath $source -Destination (Join-Path $fixtureData $name) }
    }
    $env:PIINPUT_HOST_INSTANCE = "soak_$PID"
    $env:PIINPUT_PACKAGE_DATA_DIR = $fixtureData
    $env:PIINPUT_USER_DATA_DIR = $fixtureUser
    $server = Start-Process -FilePath $HostExe -ArgumentList "--serve" -PassThru -WindowStyle Hidden
    $health = @()
    foreach ($attempt in 1..100) {
        $health = @(& $HostExe --health 2>&1)
        if ($LASTEXITCODE -eq 0) { break }
        Start-Sleep -Milliseconds 50
    }
    if ($LASTEXITCODE -ne 0 -or ($health -join "`n") -notmatch "lexicon_storage=mmap") {
        throw "Soak Host did not become healthy with a mapped lexicon: $($health -join ' ')"
    }
    $healthText = $health -join "`n"
    $buildId = ((& $HostExe --build-id 2>&1) | Select-Object -First 1).Trim()
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($buildId)) {
        throw "Soak Host build identity could not be read."
    }

    # Warm every bounded cache before setting the baseline. In particular, each
    # fixture process has a distinct client identity, so a handful of requests
    # leaves the 64-entry Host session cache mostly empty. Measuring while that
    # cache fills turns expected bounded growth into a false leak slope.
    Invoke-SoakClient @("--transport-burst", "200") "Transport warm-up"
    Invoke-SoakClient @("shurushuchuneicunguanlidanyuan") "Chinese warm-up"
    Invoke-SoakClient @("--english-burst", "200") "English warm-up"
    foreach ($warmup in 1..80) {
        Invoke-SoakClient @("wo") "Session-cache warm-up iteration $warmup"
    }
    foreach ($warmup in 1..20) {
        Invoke-SoakClient @("--transport-burst", "200") "Steady transport warm-up iteration $warmup"
        Invoke-SoakClient @("wo") "Steady Chinese warm-up iteration $warmup"
        if (($warmup % 10) -eq 0) {
            Invoke-SoakClient @("--english-burst", "200") "Steady English warm-up iteration $warmup"
        }
    }
    Start-Sleep -Seconds 2

    $clock = [Diagnostics.Stopwatch]::StartNew()
    $deadline = [TimeSpan]::FromHours($DurationHours)
    $iteration = 0
    while ($clock.Elapsed -lt $deadline) {
        ++$iteration
        Invoke-SoakClient @("--transport-burst", "200") "Transport workload iteration $iteration"
        Invoke-SoakClient @("wo") "Chinese workload iteration $iteration"
        if (($iteration % 10) -eq 0) {
            Invoke-SoakClient @("--english-burst", "200") "English workload iteration $iteration"
        }
        $process = Get-Process -Id $server.Id -ErrorAction Stop
        $row = [pscustomobject]@{
            TimestampUtc = [DateTime]::UtcNow.ToString("o")
            ElapsedHours = [math]::Round($clock.Elapsed.TotalHours, 6)
            PrivateBytes = [int64]$process.PrivateMemorySize64
            WorkingSetBytes = [int64]$process.WorkingSet64
            VirtualBytes = [int64]$process.VirtualMemorySize64
            Handles = [int]$process.HandleCount
            Threads = [int]$process.Threads.Count
            CpuSeconds = [double]$process.CPU
        }
        $rows.Add($row)
        Write-SoakSample $row $samplesPath
        $remainingMs = [math]::Floor(($deadline - $clock.Elapsed).TotalMilliseconds)
        if ($remainingMs -gt 0) { Start-Sleep -Milliseconds ([math]::Min($SampleSeconds * 1000, $remainingMs)) }
    }
    $clock.Stop()
    if ($rows.Count -lt 2) { throw "Soak test did not collect enough samples." }
    $baseline = $rows[0]
    $last = $rows[$rows.Count - 1]
    $privateGrowth = [int64]$last.PrivateBytes - [int64]$baseline.PrivateBytes
    $workingGrowth = [int64]$last.WorkingSetBytes - [int64]$baseline.WorkingSetBytes
    $handleGrowth = [int]$last.Handles - [int]$baseline.Handles
    $privateSlope = Get-SlopePerHour $rows.ToArray() "PrivateBytes"
    $handleSlope = Get-SlopePerHour $rows.ToArray() "Handles"
    if ($privateGrowth -gt $MaxPrivateGrowthBytes) { throw "Private bytes grew by $privateGrowth, over $MaxPrivateGrowthBytes." }
    if ($workingGrowth -gt $MaxWorkingSetGrowthBytes) { throw "Working set grew by $workingGrowth, over $MaxWorkingSetGrowthBytes." }
    if ($handleGrowth -gt $MaxHandleGrowth) { throw "Handle count grew by $handleGrowth, over $MaxHandleGrowth." }
    if ($DurationHours -ge $MinSlopeEvaluationHours -and
        $privateSlope -gt $MaxPrivateSlopeBytesPerHour) {
        throw "Private-byte slope $privateSlope bytes/hour exceeds $MaxPrivateSlopeBytesPerHour."
    }
    if ($DurationHours -ge $MinSlopeEvaluationHours -and
        $handleSlope -gt $MaxHandleSlopePerHour) {
        throw "Handle slope $handleSlope/hour exceeds $MaxHandleSlopePerHour."
    }
} catch {
    $failure = $_
} finally {
    $endedUtc = [DateTime]::UtcNow
    if ($null -ne $server -and -not $server.HasExited) {
        & $HostExe --drain *> $null
        if (-not $server.WaitForExit(5000)) { Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue }
    }
    $summary = [ordered]@{
        status = if ($null -eq $failure) { "passed" } else { "failed" }
        error = if ($null -eq $failure) { "" } else { $failure.Exception.Message }
        build_id = $buildId
        lexicon_storage = if ($healthText -match "(?m)^lexicon_storage=(.+)$") { $Matches[1].Trim() } else { "unknown" }
        lexicon_mapped_bytes = if ($healthText -match "(?m)^lexicon_mapped_bytes=(\d+)$") { [int64]$Matches[1] } else { 0 }
        requested_hours = $DurationHours
        started_utc = $startedUtc.ToString("o")
        ended_utc = $endedUtc.ToString("o")
        sample_count = $rows.Count
        baseline_private_bytes = if ($rows.Count) { $rows[0].PrivateBytes } else { 0 }
        final_private_bytes = if ($rows.Count) { $rows[$rows.Count - 1].PrivateBytes } else { 0 }
        baseline_working_set_bytes = if ($rows.Count) { $rows[0].WorkingSetBytes } else { 0 }
        final_working_set_bytes = if ($rows.Count) { $rows[$rows.Count - 1].WorkingSetBytes } else { 0 }
        peak_private_bytes = if ($rows.Count) { ($rows | Measure-Object PrivateBytes -Maximum).Maximum } else { 0 }
        peak_working_set_bytes = if ($rows.Count) { ($rows | Measure-Object WorkingSetBytes -Maximum).Maximum } else { 0 }
        baseline_handles = if ($rows.Count) { $rows[0].Handles } else { 0 }
        final_handles = if ($rows.Count) { $rows[$rows.Count - 1].Handles } else { 0 }
        peak_handles = if ($rows.Count) { ($rows | Measure-Object Handles -Maximum).Maximum } else { 0 }
        private_growth_bytes = if ($rows.Count) { [int64]$rows[$rows.Count - 1].PrivateBytes - [int64]$rows[0].PrivateBytes } else { 0 }
        working_set_growth_bytes = if ($rows.Count) { [int64]$rows[$rows.Count - 1].WorkingSetBytes - [int64]$rows[0].WorkingSetBytes } else { 0 }
        handle_growth = if ($rows.Count) { [int]$rows[$rows.Count - 1].Handles - [int]$rows[0].Handles } else { 0 }
        private_slope_bytes_per_hour = if ($rows.Count -gt 1) { Get-SlopePerHour $rows.ToArray() "PrivateBytes" } else { 0 }
        handle_slope_per_hour = if ($rows.Count -gt 1) { Get-SlopePerHour $rows.ToArray() "Handles" } else { 0 }
        slope_evaluated = $DurationHours -ge $MinSlopeEvaluationHours
        limits = [ordered]@{
            max_private_growth_bytes = $MaxPrivateGrowthBytes
            max_working_set_growth_bytes = $MaxWorkingSetGrowthBytes
            max_handle_growth = $MaxHandleGrowth
            max_private_slope_bytes_per_hour = $MaxPrivateSlopeBytesPerHour
            max_handle_slope_per_hour = $MaxHandleSlopePerHour
            min_slope_evaluation_hours = $MinSlopeEvaluationHours
        }
    }
    $summary | ConvertTo-Json | Set-Content -LiteralPath $summaryPath -Encoding UTF8
    $env:PIINPUT_HOST_INSTANCE = $previousInstance
    $env:PIINPUT_PACKAGE_DATA_DIR = $previousPackageData
    $env:PIINPUT_USER_DATA_DIR = $previousUserData
    if (Test-Path -LiteralPath $fixture) { Remove-Item -LiteralPath $fixture -Recurse -Force -ErrorAction SilentlyContinue }
}
if ($null -ne $failure) { throw $failure }
Write-Host "PiInput Host soak passed: $DurationHours hours, $($rows.Count) samples. Report: $summaryPath" -ForegroundColor Green
