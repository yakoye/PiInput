param(
    [string]$Version = "",
    [string]$VerificationDocument = "",
    [string]$ReportPath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if ([string]::IsNullOrWhiteSpace($Version)) {
    $Version = (Get-Content -Raw -LiteralPath (Join-Path $Root "VERSION")).Trim()
}
if ([string]::IsNullOrWhiteSpace($VerificationDocument)) {
    $VerificationDocument = Join-Path $Root "docs/VERIFICATION_v$Version.md"
}
$document = [IO.Path]::GetFullPath($VerificationDocument)
function Write-ReleaseEvidence([System.Collections.IDictionary]$Result) {
    $json = $Result | ConvertTo-Json -Depth 3
    if ([string]::IsNullOrWhiteSpace($ReportPath)) {
        $json
        return
    }
    $resolvedReport = [IO.Path]::GetFullPath($ReportPath)
    $reportParent = Split-Path -Parent $resolvedReport
    if (-not [string]::IsNullOrWhiteSpace($reportParent)) {
        New-Item -ItemType Directory -Force -Path $reportParent | Out-Null
    }
    $json | Set-Content -LiteralPath $resolvedReport -Encoding UTF8
}

$observed = [ordered]@{}
try {
    if (-not (Test-Path -LiteralPath $document -PathType Leaf)) {
        throw "Release verification document is missing: $document"
    }
    $text = Get-Content -Raw -LiteralPath $document
    if ($text -notmatch "(?m)^# PiInput v$([regex]::Escape($Version)) 验证记录\s*$") {
        throw "Release verification document does not identify PiInput v$Version."
    }

    $required = @("host_soak_8h", "tsf_app_soak_8h", "p0_real_host_matrix")
    foreach ($gate in $required) {
        $match = [regex]::Match($text, "(?m)^$gate=(PASS|FAIL|NOT_RUN|BLOCKED)\s*$")
        if (-not $match.Success) {
            throw "Release verification document is missing machine gate: $gate"
        }
        $observed[$gate] = $match.Groups[1].Value
    }
    $blocked = @($observed.GetEnumerator() | Where-Object Value -ne "PASS")
    if ($blocked.Count -gt 0) {
        $details = $blocked | ForEach-Object { "$($_.Key)=$($_.Value)" }
        throw "Release evidence gate is not complete: $($details -join ', ')"
    }
    Write-ReleaseEvidence ([ordered]@{
        status = "passed"
        version = $Version
        verification_document = $document
        gates = $observed
    })
} catch {
    Write-ReleaseEvidence ([ordered]@{
        status = "failed"
        version = $Version
        verification_document = $document
        gates = $observed
        error = $_.Exception.Message
    })
    throw
}
