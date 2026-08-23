param(
    [Parameter(Mandatory = $true)][string]$InputRoot,
    [string]$PfxPath = "",
    [string]$PfxPassword = "",
    [string]$CertificateThumbprint = "",
    [string]$TimestampUrl = "http://timestamp.digicert.com",
    [string]$ReportPath = "",
    [switch]$VerifyOnly
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Find-SignTool {
    $command = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $kits = Join-Path ${env:ProgramFiles(x86)} "Windows Kits/10/bin"
    if (Test-Path -LiteralPath $kits -PathType Container) {
        $candidate = Get-ChildItem -LiteralPath $kits -Directory |
            Sort-Object Name -Descending |
            ForEach-Object { Join-Path $_.FullName "x64/signtool.exe" } |
            Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
            Select-Object -First 1
        if ($candidate) { return $candidate }
    }
    throw "signtool.exe was not found in PATH or the Windows SDK."
}

$resolvedRoot = [IO.Path]::GetFullPath($InputRoot)
if (-not (Test-Path -LiteralPath $resolvedRoot -PathType Container)) {
    throw "Signing input root does not exist: $resolvedRoot"
}
$files = @(Get-ChildItem -LiteralPath $resolvedRoot -Recurse -File |
    Where-Object { $_.Extension -in @(".exe", ".dll") } |
    Sort-Object FullName)
if ($files.Count -eq 0) { throw "No PE binaries were found under $resolvedRoot" }
$signTool = Find-SignTool

if (-not $VerifyOnly) {
    $identity = @()
    if (-not [string]::IsNullOrWhiteSpace($PfxPath)) {
        if (-not (Test-Path -LiteralPath $PfxPath -PathType Leaf)) {
            throw "Signing PFX does not exist: $PfxPath"
        }
        $identity = @("/f", [IO.Path]::GetFullPath($PfxPath))
        if (-not [string]::IsNullOrEmpty($PfxPassword)) {
            $identity += @("/p", $PfxPassword)
        }
    } elseif (-not [string]::IsNullOrWhiteSpace($CertificateThumbprint)) {
        $identity = @("/sha1", ($CertificateThumbprint -replace '\s', ''))
    } else {
        throw "Provide -PfxPath or -CertificateThumbprint when signing."
    }
    foreach ($file in $files) {
        & $signTool sign /v /fd SHA256 /td SHA256 /tr $TimestampUrl @identity $file.FullName
        if ($LASTEXITCODE -ne 0) { throw "Signing failed: $($file.FullName)" }
    }
}

$verificationRows = foreach ($file in $files) {
    & $signTool verify /pa /all /v $file.FullName | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Authenticode verification failed: $($file.FullName)" }
    $signature = Get-AuthenticodeSignature -LiteralPath $file.FullName
    if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid) {
        throw "Authenticode status is $($signature.Status): $($file.FullName)"
    }
    if ($null -eq $signature.SignerCertificate) {
        throw "Authenticode signer certificate is missing: $($file.FullName)"
    }
    if ($null -eq $signature.TimeStamperCertificate) {
        throw "RFC 3161 timestamp certificate is missing: $($file.FullName)"
    }
    [pscustomobject]@{
        File = [IO.Path]::GetRelativePath($resolvedRoot, $file.FullName)
        Status = $signature.Status.ToString()
        SignerSubject = [string]$signature.SignerCertificate.Subject
        SignerThumbprint = [string]$signature.SignerCertificate.Thumbprint
        TimestampSubject = [string]$signature.TimeStamperCertificate.Subject
        TimestampThumbprint = [string]$signature.TimeStamperCertificate.Thumbprint
        Sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}
if (-not [string]::IsNullOrWhiteSpace($ReportPath)) {
    $resolvedReport = [IO.Path]::GetFullPath($ReportPath)
    $reportParent = Split-Path -Parent $resolvedReport
    if (-not [string]::IsNullOrWhiteSpace($reportParent)) {
        New-Item -ItemType Directory -Force -Path $reportParent | Out-Null
    }
    $verificationRows | ConvertTo-Json -Depth 3 |
        Set-Content -LiteralPath $resolvedReport -Encoding UTF8
}
Write-Host "Authenticode verified: $($files.Count) PE binaries under $resolvedRoot" -ForegroundColor Green
