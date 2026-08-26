param(
    [Parameter(Mandatory = $true)][string]$ControllerExe,
    [string]$ExpectedTsf = "",
    [switch]$FixtureMode,
    [double]$DurationHours = 8.0,
    [int]$SampleSeconds = 30,
    [string]$OutputDirectory = "",
    [int64]$MaxPrivateGrowthBytes = 67108864,
    [int64]$MaxWorkingSetGrowthBytes = 100663296,
    [int]$MaxHandleGrowth = 64,
    [int]$MaxGdiGrowth = 64,
    [int]$MaxUserGrowth = 64,
    [double]$MaxPrivateSlopeBytesPerHour = 8388608,
    [double]$MaxHandleSlopePerHour = 4.0,
    [int64]$MaxHostPrivateGrowthBytes = 67108864,
    [int64]$MaxHostWorkingSetGrowthBytes = 100663296,
    [int]$MaxHostHandleGrowth = 64,
    [double]$MaxHostPrivateSlopeBytesPerHour = 8388608,
    [double]$MaxHostHandleSlopePerHour = 4.0,
    [double]$MinIterationsPerHour = 100.0,
    [double]$MinSlopeEvaluationHours = 1.0
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
if ($DurationHours -le 0 -or $SampleSeconds -lt 1 -or $MinIterationsPerHour -le 0) {
    throw "DurationHours, SampleSeconds and MinIterationsPerHour must be positive."
}
foreach ($required in @($ControllerExe)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Missing TSF/App soak input: $required"
    }
}
$ControllerExe = [IO.Path]::GetFullPath($ControllerExe)
if (-not $FixtureMode) {
    if ([string]::IsNullOrWhiteSpace($ExpectedTsf) -or
        -not (Test-Path -LiteralPath $ExpectedTsf -PathType Leaf)) {
        throw "ExpectedTsf is required outside FixtureMode."
    }
    $ExpectedTsf = [IO.Path]::GetFullPath($ExpectedTsf)
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) "artifacts/tsf-app-soak"
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$statusPath = Join-Path $OutputDirectory "controller-status.json"
$samplesPath = Join-Path $OutputDirectory "app-resource-samples.csv"
$summaryPath = Join-Path $OutputDirectory "summary.json"
$stdoutPath = Join-Path $OutputDirectory "stdout.log"
$stderrPath = Join-Path $OutputDirectory "stderr.log"
foreach ($outputPath in @(
        $statusPath, $samplesPath, $summaryPath, $stdoutPath, $stderrPath)) {
    if (Test-Path -LiteralPath $outputPath -PathType Leaf) {
        Remove-Item -LiteralPath $outputPath -Force
    }
}

if (-not ("PiInput.Tests.GuiResources" -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
namespace PiInput.Tests {
    public static class GuiResources {
        [DllImport("user32.dll", SetLastError = true)]
        public static extern uint GetGuiResources(IntPtr process, uint flags);
    }
}
"@
}

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

function Read-ControllerStatus([string]$Path) {
    foreach ($attempt in 1..20) {
        try {
            if (Test-Path -LiteralPath $Path -PathType Leaf) {
                return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
            }
        } catch [IO.IOException], [System.Management.Automation.PSInvalidOperationException] {
            if ($attempt -eq 20) { throw }
        }
        Start-Sleep -Milliseconds 100
    }
    return $null
}

function Get-RegisteredHostProcess {
    $runtime = Get-Item -LiteralPath "HKCU:\Software\PiInput\Runtime" -ErrorAction SilentlyContinue
    if ($null -eq $runtime) { return $null }
    $hostPath = [string]$runtime.GetValue("CurrentHostPath")
    if ([string]::IsNullOrWhiteSpace($hostPath)) { return $null }
    $expected = [IO.Path]::GetFullPath($hostPath)
    $health = @(& $expected --health 2>$null)
    if ($LASTEXITCODE -ne 0) { return $null }
    $healthText = $health -join "`n"
    if ($healthText -notmatch "(?m)^host_pid=([1-9][0-9]*)$") { return $null }
    $serverPid = [int]$Matches[1]
    $process = Get-Process -Id $serverPid -ErrorAction SilentlyContinue
    if ($null -eq $process) { return $null }
    try {
        if ([IO.Path]::GetFullPath($process.Path).Equals(
                $expected, [StringComparison]::OrdinalIgnoreCase)) {
            return $process
        }
    } catch { }
    return $null
}

$rows = [Collections.Generic.List[object]]::new()
$controller = $null
$failure = $null
$startedUtc = [DateTime]::UtcNow
$controllerOutput = ""
$controllerError = ""
$durationSeconds = [Math]::Max(1, [Math]::Ceiling($DurationHours * 3600.0))
$previousHostInstance = $env:PIINPUT_HOST_INSTANCE
if (-not $FixtureMode) { $env:PIINPUT_HOST_INSTANCE = $null }
try {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $ControllerExe
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $arguments = [Collections.Generic.List[string]]::new()
    if (-not $FixtureMode) {
        $arguments.Add("--piinput-smoke")
        $arguments.Add("--expected-tsf")
        $arguments.Add($ExpectedTsf)
    }
    foreach ($argument in @(
            "--soak-seconds", [string]$durationSeconds,
            "--status-file", $statusPath)) {
        $arguments.Add($argument)
    }
    foreach ($argument in $arguments) {
        $start.ArgumentList.Add($argument)
    }
    $controller = [Diagnostics.Process]::new()
    $controller.StartInfo = $start
    if (-not $controller.Start()) { throw "Could not start the Controlled TSF controller." }

    $status = $null
    foreach ($attempt in 1..600) {
        if ($controller.HasExited) { break }
        $status = Read-ControllerStatus $statusPath
        if ($null -ne $status -and [int]$status.iterations -ge 5) { break }
        Start-Sleep -Milliseconds 100
    }
    if ($controller.HasExited) {
        throw "Controlled TSF controller exited before the soak baseline."
    }
    if ($null -eq $status -or [int]$status.iterations -lt 5) {
        throw "Controlled TSF controller did not publish a warm baseline."
    }

    $clock = [Diagnostics.Stopwatch]::StartNew()
    while (-not $controller.HasExited) {
        $status = Read-ControllerStatus $statusPath
        if ($null -eq $status) { throw "Controlled TSF status disappeared." }
        if (-not [bool]$status.pass) { throw "Controlled TSF workload reported a failure." }
        $app = Get-Process -Id ([int]$status.app_pid) -ErrorAction Stop
        $hostProcess = Get-RegisteredHostProcess
        if (-not $FixtureMode -and $null -eq $hostProcess) {
            throw "The registered PiInput Host process could not be resolved."
        }
        $row = [pscustomobject]@{
            TimestampUtc = [DateTime]::UtcNow.ToString("o")
            ElapsedHours = [Math]::Round($clock.Elapsed.TotalHours, 6)
            Iterations = [int]$status.iterations
            ContextRecreates = [int]$status.context_recreates
            AppPrivateBytes = [int64]$app.PrivateMemorySize64
            AppWorkingSetBytes = [int64]$app.WorkingSet64
            AppVirtualBytes = [int64]$app.VirtualMemorySize64
            AppHandles = [int]$app.HandleCount
            AppThreads = [int]$app.Threads.Count
            AppCpuSeconds = [double]$app.CPU
            AppGdiObjects = [int][PiInput.Tests.GuiResources]::GetGuiResources($app.Handle, 0U)
            AppUserObjects = [int][PiInput.Tests.GuiResources]::GetGuiResources($app.Handle, 1U)
            HostPid = if ($null -ne $hostProcess) { [int]$hostProcess.Id } else { 0 }
            HostPrivateBytes = if ($null -ne $hostProcess) { [int64]$hostProcess.PrivateMemorySize64 } else { 0 }
            HostWorkingSetBytes = if ($null -ne $hostProcess) { [int64]$hostProcess.WorkingSet64 } else { 0 }
            HostHandles = if ($null -ne $hostProcess) { [int]$hostProcess.HandleCount } else { 0 }
            HostThreads = if ($null -ne $hostProcess) { [int]$hostProcess.Threads.Count } else { 0 }
            HostCpuSeconds = if ($null -ne $hostProcess) { [double]$hostProcess.CPU } else { 0 }
        }
        $rows.Add($row)
        Write-SoakSample $row $samplesPath
        Start-Sleep -Seconds $SampleSeconds
        $controller.Refresh()
    }
    $clock.Stop()
    $controllerOutput = $controller.StandardOutput.ReadToEnd()
    $controllerError = $controller.StandardError.ReadToEnd()
    Set-Content -LiteralPath $stdoutPath -Value $controllerOutput -Encoding UTF8
    Set-Content -LiteralPath $stderrPath -Value $controllerError -Encoding UTF8
    if ($controller.ExitCode -ne 0) {
        throw "Controlled TSF controller failed with exit code $($controller.ExitCode): $controllerOutput $controllerError"
    }
    $finalStatus = Read-ControllerStatus $statusPath
    if ($null -eq $finalStatus -or -not [bool]$finalStatus.pass -or
        -not [bool]$finalStatus.completed) {
        throw "Controlled TSF controller did not publish a completed passing status."
    }
    $minimumIterations = [Math]::Max(5, [Math]::Ceiling($DurationHours * $MinIterationsPerHour))
    if ([int]$finalStatus.iterations -lt $minimumIterations) {
        throw "Controlled TSF workload completed only $($finalStatus.iterations) iterations; at least $minimumIterations are required."
    }
    $minimumContextRecreates = 1 + [Math]::Floor([int]$finalStatus.iterations / 20)
    if ([int]$finalStatus.context_recreates -lt $minimumContextRecreates) {
        throw "Controlled TSF workload published only $($finalStatus.context_recreates) context recreates; at least $minimumContextRecreates are required."
    }
    if ($rows.Count -lt 2) { throw "TSF/App soak did not collect enough samples." }

    $baseline = $rows[0]
    $last = $rows[$rows.Count - 1]
    $privateGrowth = [int64]$last.AppPrivateBytes - [int64]$baseline.AppPrivateBytes
    $workingGrowth = [int64]$last.AppWorkingSetBytes - [int64]$baseline.AppWorkingSetBytes
    $handleGrowth = [int]$last.AppHandles - [int]$baseline.AppHandles
    $gdiGrowth = [int]$last.AppGdiObjects - [int]$baseline.AppGdiObjects
    $userGrowth = [int]$last.AppUserObjects - [int]$baseline.AppUserObjects
    $privateSlope = Get-SlopePerHour $rows.ToArray() "AppPrivateBytes"
    $handleSlope = Get-SlopePerHour $rows.ToArray() "AppHandles"
    $hostPrivateGrowth = [int64]$last.HostPrivateBytes - [int64]$baseline.HostPrivateBytes
    $hostWorkingGrowth = [int64]$last.HostWorkingSetBytes - [int64]$baseline.HostWorkingSetBytes
    $hostHandleGrowth = [int]$last.HostHandles - [int]$baseline.HostHandles
    $hostPrivateSlope = Get-SlopePerHour $rows.ToArray() "HostPrivateBytes"
    $hostHandleSlope = Get-SlopePerHour $rows.ToArray() "HostHandles"
    if ($privateGrowth -gt $MaxPrivateGrowthBytes) { throw "App private bytes grew by $privateGrowth, over $MaxPrivateGrowthBytes." }
    if ($workingGrowth -gt $MaxWorkingSetGrowthBytes) { throw "App working set grew by $workingGrowth, over $MaxWorkingSetGrowthBytes." }
    if ($handleGrowth -gt $MaxHandleGrowth) { throw "App handles grew by $handleGrowth, over $MaxHandleGrowth." }
    if ($gdiGrowth -gt $MaxGdiGrowth) { throw "App GDI objects grew by $gdiGrowth, over $MaxGdiGrowth." }
    if ($userGrowth -gt $MaxUserGrowth) { throw "App USER objects grew by $userGrowth, over $MaxUserGrowth." }
    if ($DurationHours -ge $MinSlopeEvaluationHours -and
        $privateSlope -gt $MaxPrivateSlopeBytesPerHour) {
        throw "App private-byte slope $privateSlope bytes/hour exceeds $MaxPrivateSlopeBytesPerHour."
    }
    if ($DurationHours -ge $MinSlopeEvaluationHours -and
        $handleSlope -gt $MaxHandleSlopePerHour) {
        throw "App handle slope $handleSlope/hour exceeds $MaxHandleSlopePerHour."
    }
    if (-not $FixtureMode) {
        if ($hostPrivateGrowth -gt $MaxHostPrivateGrowthBytes) {
            throw "Host private bytes grew by $hostPrivateGrowth, over $MaxHostPrivateGrowthBytes."
        }
        if ($hostWorkingGrowth -gt $MaxHostWorkingSetGrowthBytes) {
            throw "Host working set grew by $hostWorkingGrowth, over $MaxHostWorkingSetGrowthBytes."
        }
        if ($hostHandleGrowth -gt $MaxHostHandleGrowth) {
            throw "Host handles grew by $hostHandleGrowth, over $MaxHostHandleGrowth."
        }
        if ($DurationHours -ge $MinSlopeEvaluationHours -and
            $hostPrivateSlope -gt $MaxHostPrivateSlopeBytesPerHour) {
            throw "Host private-byte slope $hostPrivateSlope bytes/hour exceeds $MaxHostPrivateSlopeBytesPerHour."
        }
        if ($DurationHours -ge $MinSlopeEvaluationHours -and
            $hostHandleSlope -gt $MaxHostHandleSlopePerHour) {
            throw "Host handle slope $hostHandleSlope/hour exceeds $MaxHostHandleSlopePerHour."
        }
    }
} catch {
    $failure = $_
} finally {
    $env:PIINPUT_HOST_INSTANCE = $previousHostInstance
    $endedUtc = [DateTime]::UtcNow
    if ($null -ne $controller -and -not $controller.HasExited) {
        $controller.Kill($true)
        $null = $controller.WaitForExit(5000)
    }
    if ($null -ne $controller -and $controller.HasExited -and
        [string]::IsNullOrEmpty($controllerOutput)) {
        $controllerOutput = $controller.StandardOutput.ReadToEnd()
        $controllerError = $controller.StandardError.ReadToEnd()
        Set-Content -LiteralPath $stdoutPath -Value $controllerOutput -Encoding UTF8
        Set-Content -LiteralPath $stderrPath -Value $controllerError -Encoding UTF8
    }
    $summary = [ordered]@{
        status = if ($null -eq $failure) { "passed" } else { "failed" }
        error = if ($null -eq $failure) { "" } else { $failure.Exception.Message }
        expected_tsf = $ExpectedTsf
        fixture_mode = [bool]$FixtureMode
        requested_hours = $DurationHours
        started_utc = $startedUtc.ToString("o")
        ended_utc = $endedUtc.ToString("o")
        sample_count = $rows.Count
        final_iterations = if (Test-Path -LiteralPath $statusPath) { (Read-ControllerStatus $statusPath).iterations } else { 0 }
        final_context_recreates = if (Test-Path -LiteralPath $statusPath) { (Read-ControllerStatus $statusPath).context_recreates } else { 0 }
        app_pid = if ($rows.Count) { (Read-ControllerStatus $statusPath).app_pid } else { 0 }
        baseline_private_bytes = if ($rows.Count) { $rows[0].AppPrivateBytes } else { 0 }
        final_private_bytes = if ($rows.Count) { $rows[$rows.Count - 1].AppPrivateBytes } else { 0 }
        peak_private_bytes = if ($rows.Count) { ($rows | Measure-Object AppPrivateBytes -Maximum).Maximum } else { 0 }
        baseline_working_set_bytes = if ($rows.Count) { $rows[0].AppWorkingSetBytes } else { 0 }
        final_working_set_bytes = if ($rows.Count) { $rows[$rows.Count - 1].AppWorkingSetBytes } else { 0 }
        peak_working_set_bytes = if ($rows.Count) { ($rows | Measure-Object AppWorkingSetBytes -Maximum).Maximum } else { 0 }
        baseline_handles = if ($rows.Count) { $rows[0].AppHandles } else { 0 }
        final_handles = if ($rows.Count) { $rows[$rows.Count - 1].AppHandles } else { 0 }
        peak_handles = if ($rows.Count) { ($rows | Measure-Object AppHandles -Maximum).Maximum } else { 0 }
        baseline_gdi_objects = if ($rows.Count) { $rows[0].AppGdiObjects } else { 0 }
        final_gdi_objects = if ($rows.Count) { $rows[$rows.Count - 1].AppGdiObjects } else { 0 }
        baseline_user_objects = if ($rows.Count) { $rows[0].AppUserObjects } else { 0 }
        final_user_objects = if ($rows.Count) { $rows[$rows.Count - 1].AppUserObjects } else { 0 }
        private_growth_bytes = if ($rows.Count) { [int64]$rows[$rows.Count - 1].AppPrivateBytes - [int64]$rows[0].AppPrivateBytes } else { 0 }
        working_set_growth_bytes = if ($rows.Count) { [int64]$rows[$rows.Count - 1].AppWorkingSetBytes - [int64]$rows[0].AppWorkingSetBytes } else { 0 }
        handle_growth = if ($rows.Count) { [int]$rows[$rows.Count - 1].AppHandles - [int]$rows[0].AppHandles } else { 0 }
        gdi_growth = if ($rows.Count) { [int]$rows[$rows.Count - 1].AppGdiObjects - [int]$rows[0].AppGdiObjects } else { 0 }
        user_growth = if ($rows.Count) { [int]$rows[$rows.Count - 1].AppUserObjects - [int]$rows[0].AppUserObjects } else { 0 }
        host_pid = if ($rows.Count) { $rows[$rows.Count - 1].HostPid } else { 0 }
        host_baseline_private_bytes = if ($rows.Count) { $rows[0].HostPrivateBytes } else { 0 }
        host_final_private_bytes = if ($rows.Count) { $rows[$rows.Count - 1].HostPrivateBytes } else { 0 }
        host_peak_private_bytes = if ($rows.Count) { ($rows | Measure-Object HostPrivateBytes -Maximum).Maximum } else { 0 }
        host_baseline_working_set_bytes = if ($rows.Count) { $rows[0].HostWorkingSetBytes } else { 0 }
        host_final_working_set_bytes = if ($rows.Count) { $rows[$rows.Count - 1].HostWorkingSetBytes } else { 0 }
        host_peak_working_set_bytes = if ($rows.Count) { ($rows | Measure-Object HostWorkingSetBytes -Maximum).Maximum } else { 0 }
        host_baseline_handles = if ($rows.Count) { $rows[0].HostHandles } else { 0 }
        host_final_handles = if ($rows.Count) { $rows[$rows.Count - 1].HostHandles } else { 0 }
        host_peak_handles = if ($rows.Count) { ($rows | Measure-Object HostHandles -Maximum).Maximum } else { 0 }
        host_private_growth_bytes = if ($rows.Count) { [int64]$rows[$rows.Count - 1].HostPrivateBytes - [int64]$rows[0].HostPrivateBytes } else { 0 }
        host_working_set_growth_bytes = if ($rows.Count) { [int64]$rows[$rows.Count - 1].HostWorkingSetBytes - [int64]$rows[0].HostWorkingSetBytes } else { 0 }
        host_handle_growth = if ($rows.Count) { [int]$rows[$rows.Count - 1].HostHandles - [int]$rows[0].HostHandles } else { 0 }
        host_private_slope_bytes_per_hour = if ($rows.Count -gt 1) { Get-SlopePerHour $rows.ToArray() "HostPrivateBytes" } else { 0 }
        host_handle_slope_per_hour = if ($rows.Count -gt 1) { Get-SlopePerHour $rows.ToArray() "HostHandles" } else { 0 }
        private_slope_bytes_per_hour = if ($rows.Count -gt 1) { Get-SlopePerHour $rows.ToArray() "AppPrivateBytes" } else { 0 }
        handle_slope_per_hour = if ($rows.Count -gt 1) { Get-SlopePerHour $rows.ToArray() "AppHandles" } else { 0 }
        slope_evaluated = $DurationHours -ge $MinSlopeEvaluationHours
        limits = [ordered]@{
            max_private_growth_bytes = $MaxPrivateGrowthBytes
            max_working_set_growth_bytes = $MaxWorkingSetGrowthBytes
            max_handle_growth = $MaxHandleGrowth
            max_gdi_growth = $MaxGdiGrowth
            max_user_growth = $MaxUserGrowth
            max_private_slope_bytes_per_hour = $MaxPrivateSlopeBytesPerHour
            max_handle_slope_per_hour = $MaxHandleSlopePerHour
            max_host_private_growth_bytes = $MaxHostPrivateGrowthBytes
            max_host_working_set_growth_bytes = $MaxHostWorkingSetGrowthBytes
            max_host_handle_growth = $MaxHostHandleGrowth
            max_host_private_slope_bytes_per_hour = $MaxHostPrivateSlopeBytesPerHour
            max_host_handle_slope_per_hour = $MaxHostHandleSlopePerHour
            min_iterations_per_hour = $MinIterationsPerHour
            min_slope_evaluation_hours = $MinSlopeEvaluationHours
        }
    }
    $summary | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $summaryPath -Encoding UTF8
}
if ($null -ne $failure) { throw $failure }
Write-Host "PiInput TSF/App soak passed: $DurationHours hours, $($rows.Count) samples. Report: $summaryPath" -ForegroundColor Green
