param(
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Version = (Get-Content -Raw -LiteralPath (Join-Path $Root "VERSION")).Trim()
$Dist = Join-Path $Root "dist/windows-x64"
$Artifacts = Join-Path $Root "artifacts"
$PackageName = "PiInput-v$Version-windows-x64"
$PackageRoot = Join-Path $Artifacts $PackageName
$Zip = Join-Path $Artifacts "$PackageName.zip"

foreach ($required in @(
    (Join-Path $Dist "bin/PiInput-Install.exe"),
    (Join-Path $Dist "bin/PiInputTSF.dll"),
    (Join-Path $Dist "bin/piinput-profile.exe"),
    (Join-Path $Dist "data/base_lexicon.tsv"),
    (Join-Path $Dist "data/symbols.tsv"),
    (Join-Path $Root "docs/安装与使用指南.md")
)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Release payload is missing: $required"
    }
}

if (Test-Path -LiteralPath $PackageRoot) {
    [System.IO.Directory]::Delete($PackageRoot, $true)
}
if (Test-Path -LiteralPath $Zip) {
    [System.IO.File]::Delete($Zip)
}
New-Item -ItemType Directory -Force $PackageRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $Dist "bin") -Destination $PackageRoot -Recurse
Copy-Item -LiteralPath (Join-Path $Dist "data") -Destination $PackageRoot -Recurse
Copy-Item -LiteralPath (Join-Path $Dist "bin/PiInput-Install.exe") -Destination $PackageRoot
Copy-Item -LiteralPath (Join-Path $Root "docs/安装与使用指南.md") -Destination $PackageRoot
Copy-Item -LiteralPath (Join-Path $Root "LICENSE_NOTICE.md") -Destination $PackageRoot
Compress-Archive -LiteralPath $PackageRoot -DestinationPath $Zip -CompressionLevel Optimal

$hash = (Get-FileHash -LiteralPath $Zip -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath "$Zip.sha256.txt" -Encoding ASCII -Value "$hash  $([IO.Path]::GetFileName($Zip))"
Write-Host "Package: $Zip" -ForegroundColor Green
Write-Host "SHA-256: $hash" -ForegroundColor Green
