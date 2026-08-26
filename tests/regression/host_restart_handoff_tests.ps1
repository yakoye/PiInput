param(
    [Parameter(Mandatory = $true)]
    [string]$HostExe,
    [Parameter(Mandatory = $true)]
    [string]$ClientExe
)

$ErrorActionPreference = "Stop"
$previousInstance = $env:PIINPUT_HOST_INSTANCE
$env:PIINPUT_HOST_INSTANCE = "ctest_restart_$PID"

function Stop-TestHost {
    try { & $HostExe --drain *> $null } catch {}
}

try {
    for ($cycle = 1; $cycle -le 20; $cycle++) {
        $server = Start-Process -FilePath $HostExe -ArgumentList "--serve" -PassThru -WindowStyle Hidden
        $ready = $false
        for ($attempt = 0; $attempt -lt 80; $attempt++) {
            $health = & $HostExe --health 2>&1
            if ($LASTEXITCODE -eq 0 -and ($health -join "`n") -match "protocol=5") {
                $ready = $true
                break
            }
            Start-Sleep -Milliseconds 25
        }
        if (-not $ready) { throw "Host did not become healthy in cycle $cycle" }

        $continued = & $ClientExe --resume "w" "o" 2>&1
        if ($LASTEXITCODE -ne 0 -or ($continued -join "`n") -notmatch "raw=wo" -or
            ($continued -join "`n") -notmatch "first=我") {
            throw "Composition resume failed in cycle $cycle`: $($continued -join ' ')"
        }

        & $HostExe --drain *> $null
        if ($LASTEXITCODE -ne 0 -or -not $server.WaitForExit(5000) -or $server.ExitCode -ne 0) {
            throw "Host drain failed in cycle $cycle"
        }
    }
}
finally {
    Stop-TestHost
    $env:PIINPUT_HOST_INSTANCE = $previousInstance
}

Write-Host "PiInput Host restart/resume tests passed (20/20 cycles)."
