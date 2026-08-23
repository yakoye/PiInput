param(
    [string]$Configuration = "Release",
    [string]$Version = "",
    [switch]$RequireSigned
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$VersionFile = (Get-Content -Raw -LiteralPath (Join-Path $Root "VERSION")).Trim()
if ($Version -eq "") { $Version = $VersionFile }
if ($Version -ne $VersionFile) {
    throw "打包版本 $Version 与 VERSION 文件的 $VersionFile 不一致，先同步版本号再打包。"
}
$BaseVersion = $Version -replace '-dev$', ''
$QuickGuide = Join-Path $Root "docs/v${BaseVersion}安装、使用与测试.md"
$ReleaseNotes = Join-Path $Root "docs/release_notes_v$Version.md"
$Verification = Join-Path $Root "docs/VERIFICATION_v$Version.md"
$ComparisonReport = Join-Path $Root "docs/三组文本与八向边界对照测试_2026-08-15.md"
$TypingReport = Join-Path $Root "docs/三个长文本打字测试_v$Version.md"
$Dist = Join-Path $Root "dist/windows-x64"
$Artifacts = Join-Path $Root "artifacts"
$PackageName = "PiInput-v$Version-windows-x64"
$PackageRoot = Join-Path $Artifacts $PackageName
$Zip = Join-Path $Artifacts "$PackageName.zip"

$DictionaryGuide = Join-Path $Root "docs/词库更新说明.md"
# Report every gap at once. Cutting a release usually misses several documents
# together, and one throw per run turns that into one rebuild per missing file.
$missing = @()
foreach ($required in @(
    (Join-Path $Dist "bin/PiInput-Install.exe"),
    (Join-Path $Dist "bin/PiInput-Uninstall.exe"),
    (Join-Path $Dist "bin/PiInputTSF.dll"),
    (Join-Path $Dist "bin/PiInputHost.exe"),
    (Join-Path $Dist "bin/PiInput-Settings.exe"),
    (Join-Path $Dist "bin/piinput-diagnostics.exe"),
    (Join-Path $Dist "bin/piinput-profile.exe"),
    (Join-Path $Dist "data/piinput-base.lex"),
    (Join-Path $Dist "data/symbols.tsv"),
    (Join-Path $Dist "data/host_protocol.json"),
    (Join-Path $Root "docs/安装与使用指南.md"),
    (Join-Path $Root "docs/词库查询与分段取字.md"),
    $DictionaryGuide,
    (Join-Path $Root "docs/稳定入口与无重启升级说明.md"),
    $ComparisonReport,
    $ReleaseNotes,
    $Verification,
    $TypingReport
    $QuickGuide
)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        $missing += $required.Substring($Root.Length + 1)
    }
}
if ($missing.Count -gt 0) {
    throw ("Release payload is missing:`n    " + ($missing -join "`n    "))
}

# The payload comes from dist/, which only build.ps1 refreshes. Running cmake
# --build alone leaves dist/ on the previous release and silently ships stale
# binaries under a new version number. Compare the two and refuse.
$Build = Join-Path $Root "build/windows-x64/$Configuration"
if (Test-Path -LiteralPath $Build -PathType Container) {
    foreach ($binary in @("PiInputTSF.dll", "PiInputHost.exe", "PiInput-Install.exe")) {
        $staged = Join-Path $Dist "bin/$binary"
        $built = Join-Path $Build $binary
        if (-not (Test-Path -LiteralPath $built -PathType Leaf)) { continue }
        $stagedHash = (Get-FileHash -LiteralPath $staged -Algorithm SHA256).Hash
        $builtHash = (Get-FileHash -LiteralPath $built -Algorithm SHA256).Hash
        if ($stagedHash -ne $builtHash) {
            throw ("Staged $binary does not match the current build. " +
                "Run build.ps1 before packaging so dist/ is refreshed.")
        }
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
Copy-Item -LiteralPath (Join-Path $Dist "bin/PiInput-Uninstall.exe") -Destination $PackageRoot
Copy-Item -LiteralPath (Join-Path $Dist "bin/piinput-preview.exe") -Destination (Join-Path $PackageRoot "PiInput-Test.exe")
Copy-Item -LiteralPath (Join-Path $Root "docs/安装与使用指南.md") -Destination $PackageRoot
Copy-Item -LiteralPath (Join-Path $Root "docs/词库查询与分段取字.md") -Destination $PackageRoot
Copy-Item -LiteralPath $DictionaryGuide -Destination $PackageRoot
Copy-Item -LiteralPath (Join-Path $Root "docs/稳定入口与无重启升级说明.md") -Destination $PackageRoot
Copy-Item -LiteralPath $ComparisonReport -Destination $PackageRoot
Copy-Item -LiteralPath $TypingReport -Destination $PackageRoot
Copy-Item -LiteralPath $QuickGuide -Destination $PackageRoot
Copy-Item -LiteralPath $ReleaseNotes -Destination $PackageRoot
Copy-Item -LiteralPath $Verification -Destination $PackageRoot
Copy-Item -LiteralPath (Join-Path $Root "LICENSE_NOTICE.md") -Destination $PackageRoot
if ($RequireSigned) {
    $signedFiles = @(Get-ChildItem -LiteralPath $PackageRoot -Recurse -File |
        Where-Object { $_.Extension -in @(".exe", ".dll") })
    $unsigned = @($signedFiles | Where-Object {
        (Get-AuthenticodeSignature -LiteralPath $_.FullName).Status -ne "Valid"
    })
    if ($unsigned.Count -gt 0) {
        throw "Release signing gate failed: $($unsigned.FullName -join ', ')"
    }
    $untimestamped = @($signedFiles | Where-Object {
        $null -eq (Get-AuthenticodeSignature -LiteralPath $_.FullName).TimeStamperCertificate
    })
    if ($untimestamped.Count -gt 0) {
        throw "Release timestamp gate failed: $($untimestamped.FullName -join ', ')"
    }
}
Compress-Archive -LiteralPath $PackageRoot -DestinationPath $Zip -CompressionLevel Optimal

$hash = (Get-FileHash -LiteralPath $Zip -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath "$Zip.sha256.txt" -Encoding ASCII -Value "$hash  $([IO.Path]::GetFileName($Zip))"
Write-Host "Package: $Zip" -ForegroundColor Green
Write-Host "SHA-256: $hash" -ForegroundColor Green
