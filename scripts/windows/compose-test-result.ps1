param(
    [Parameter(Mandatory = $true)]
    [string[]]$CaseResultPath,
    [Parameter(Mandatory = $true)]
    [string]$Version,
    [Parameter(Mandatory = $true)]
    [string]$BuildId,
    [Parameter(Mandatory = $true)]
    [string]$Commit,
    [switch]$Dirty,
    [string]$RunId = "",
    [string]$EnvironmentJsonPath = "",
    [string]$MetricsJsonPath = "",
    [string]$ResourcesJsonPath = "",
    [string[]]$ArtifactPath = @(),
    [Parameter(Mandatory = $true)]
    [string]$OutputPath,
    [string]$ManifestPath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Read-JsonDocument([string]$Path, [string]$Description) {
    $resolved = [IO.Path]::GetFullPath($Path)
    if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
        throw "$Description is missing: $resolved"
    }
    try {
        return Get-Content -Raw -LiteralPath $resolved -Encoding UTF8 | ConvertFrom-Json
    } catch {
        throw "$Description is not valid JSON: $resolved. $($_.Exception.Message)"
    }
}

function Write-JsonAtomically([object]$Value, [string]$Path, [int]$Depth = 8) {
    $resolved = [IO.Path]::GetFullPath($Path)
    $parent = Split-Path -Parent $resolved
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    $temporary = "$resolved.tmp-$PID"
    try {
        $Value | ConvertTo-Json -Depth $Depth | Set-Content -LiteralPath $temporary -Encoding UTF8
        Move-Item -LiteralPath $temporary -Destination $resolved -Force
    } finally {
        if (Test-Path -LiteralPath $temporary) {
            Remove-Item -LiteralPath $temporary -Force
        }
    }
}

function Read-OptionalObject([string]$Path, [object]$Default, [string]$Description) {
    if ([string]::IsNullOrWhiteSpace($Path)) { return $Default }
    $value = Read-JsonDocument $Path $Description
    if ($null -eq $value -or $value -is [array]) {
        throw "$Description must contain one JSON object: $Path"
    }
    return $value
}

function Get-Sha256([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = $algorithm.ComputeHash($stream)
        return ([BitConverter]::ToString($bytes)).Replace("-", "").ToLowerInvariant()
    } finally {
        $algorithm.Dispose()
        $stream.Dispose()
    }
}

if ([string]::IsNullOrWhiteSpace($Version) -or
    [string]::IsNullOrWhiteSpace($BuildId) -or
    [string]::IsNullOrWhiteSpace($Commit)) {
    throw "Version, BuildId and Commit must be non-empty."
}

if ([string]::IsNullOrWhiteSpace($RunId)) {
    $shortCommit = if ($Commit.Length -gt 12) { $Commit.Substring(0, 12) } else { $Commit }
    $RunId = "{0}-{1}" -f ([DateTime]::UtcNow.ToString("yyyyMMddTHHmmssZ")), $shortCommit
}

$allowedStatuses = @("PASS", "FAIL", "BLOCKED", "NOT_RUN", "N/A")
$caseIdPattern = '^[A-Z][A-Z0-9]*(?:-[A-Z0-9]+)*-[0-9]{3}$'
$seen = @{}
$cases = [System.Collections.Generic.List[object]]::new()

foreach ($casePath in $CaseResultPath) {
    $document = Read-JsonDocument $casePath "Case result"
    [object[]]$documentCases = if ($null -ne $document -and
        $document.PSObject.Properties.Name -contains "cases") {
        @($document.cases)
    } else {
        @($document)
    }
    if ($documentCases.Count -eq 0) {
        throw "Case result contains no cases: $casePath"
    }
    foreach ($case in $documentCases) {
        if ($null -eq $case -or -not ($case.PSObject.Properties.Name -contains "case_id")) {
            throw "Every case result must contain case_id: $casePath"
        }
        $caseId = [string]$case.case_id
        if ($caseId -notmatch $caseIdPattern) {
            throw "Invalid case_id '$caseId' in $casePath"
        }
        if ($seen.ContainsKey($caseId)) {
            throw "Duplicate case_id '$caseId' across result inputs."
        }
        $seen[$caseId] = $true

        if (-not ($case.PSObject.Properties.Name -contains "status")) {
            throw "Case '$caseId' is missing status."
        }
        $status = ([string]$case.status).ToUpperInvariant()
        if ($status -notin $allowedStatuses) {
            throw "Case '$caseId' has unsupported status '$status'."
        }

        $duration = 0.0
        if ($case.PSObject.Properties.Name -contains "duration_ms") {
            $duration = [double]$case.duration_ms
        }
        if ($duration -lt 0 -or [double]::IsNaN($duration) -or [double]::IsInfinity($duration)) {
            throw "Case '$caseId' has invalid duration_ms."
        }

        $failureClass = if ($case.PSObject.Properties.Name -contains "failure_class") {
            [string]$case.failure_class
        } else { "" }
        if ($status -eq "FAIL" -and [string]::IsNullOrWhiteSpace($failureClass)) {
            throw "Failed case '$caseId' must include failure_class."
        }
        $evidence = if ($case.PSObject.Properties.Name -contains "evidence") {
            @($case.evidence | ForEach-Object { [string]$_ })
        } else { @() }

        $cases.Add([ordered]@{
            case_id = $caseId
            status = $status
            duration_ms = $duration
            failure_class = $failureClass
            evidence = [object[]]@($evidence)
        })
    }
}

$counts = [ordered]@{
    pass = @($cases | Where-Object status -eq "PASS").Count
    fail = @($cases | Where-Object status -eq "FAIL").Count
    blocked = @($cases | Where-Object status -eq "BLOCKED").Count
    not_run = @($cases | Where-Object status -eq "NOT_RUN").Count
    na = @($cases | Where-Object status -eq "N/A").Count
}
$executed = $counts.pass + $counts.fail
$gateStatus = if ($counts.fail -gt 0) {
    "FAIL"
} elseif ($counts.blocked -gt 0) {
    "BLOCKED"
} elseif ($counts.not_run -gt 0 -or $cases.Count -eq 0) {
    "NOT_RUN"
} elseif ($counts.pass -gt 0) {
    "PASS"
} else {
    "N/A"
}

$environment = Read-OptionalObject $EnvironmentJsonPath ([ordered]@{
    windows = [Environment]::OSVersion.VersionString
    host = "automation"
    dpi = "unknown"
}) "Environment JSON"
$metrics = Read-OptionalObject $MetricsJsonPath ([ordered]@{}) "Metrics JSON"
$resources = Read-OptionalObject $ResourcesJsonPath ([ordered]@{}) "Resources JSON"

$resolvedOutput = [IO.Path]::GetFullPath($OutputPath)
if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
    $ManifestPath = Join-Path (Split-Path -Parent $resolvedOutput) "artifact-manifest.json"
}
$resolvedManifest = [IO.Path]::GetFullPath($ManifestPath)

$result = [ordered]@{
    schema_version = 1
    run_id = $RunId
    generated_at_utc = [DateTime]::UtcNow.ToString("o")
    build = [ordered]@{
        version = $Version
        build_id = $BuildId
        commit = $Commit
        dirty = [bool]$Dirty
    }
    environment = $environment
    cases = @($cases)
    metrics = $metrics
    resources = $resources
    summary = [ordered]@{
        total = $cases.Count
        executed = $executed
        pass = $counts.pass
        fail = $counts.fail
        blocked = $counts.blocked
        not_run = $counts.not_run
        na = $counts.na
        pass_rate = if ($executed -gt 0) { $counts.pass / [double]$executed } else { $null }
        gate_status = $gateStatus
    }
    artifact_manifest = $resolvedManifest
}
$manifestItems = [System.Collections.Generic.List[object]]::new()
$manifestInputs = @($CaseResultPath) + @($ArtifactPath)
$manifestSeen = @{}
foreach ($artifact in $manifestInputs) {
    $resolvedArtifact = [IO.Path]::GetFullPath($artifact)
    if ($manifestSeen.ContainsKey($resolvedArtifact)) { continue }
    $manifestSeen[$resolvedArtifact] = $true
    if (-not (Test-Path -LiteralPath $resolvedArtifact -PathType Leaf)) {
        throw "Artifact is missing: $resolvedArtifact"
    }
    $item = Get-Item -LiteralPath $resolvedArtifact
    $manifestItems.Add([ordered]@{
        path = $resolvedArtifact
        bytes = [int64]$item.Length
        sha256 = Get-Sha256 $resolvedArtifact
    })
}
$manifest = [ordered]@{
    schema_version = 1
    run_id = $RunId
    build_id = $BuildId
    artifacts = @($manifestItems)
}
Write-JsonAtomically $manifest $resolvedManifest
Write-JsonAtomically $result $resolvedOutput

$result | ConvertTo-Json -Depth 8
