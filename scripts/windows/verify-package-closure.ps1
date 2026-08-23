param(
    [Parameter(Mandatory = $true)][string]$PackageZip,
    [string]$ExpectedVersion = "",
    [string]$ExpectedBuildId = "",
    [switch]$RequireSigned,
    [switch]$InstallSmoke,
    [switch]$AllowExistingInstall,
    [switch]$LeaveInstalled,
    [string]$PreviousPackageZip = "",
    [string]$ControlledTsfController = "",
    [string]$ReportDirectory = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$zip = [IO.Path]::GetFullPath($PackageZip)
if (-not (Test-Path -LiteralPath $zip -PathType Leaf)) { throw "Package is missing: $zip" }
if ([string]::IsNullOrWhiteSpace($ExpectedVersion)) {
    $ExpectedVersion = (Get-Content -Raw -LiteralPath (Join-Path $Root "VERSION")).Trim()
}
if ([string]::IsNullOrWhiteSpace($ReportDirectory)) {
    $ReportDirectory = Join-Path $Root "artifacts/package-closure"
}
New-Item -ItemType Directory -Force $ReportDirectory | Out-Null

function Get-RegisteredTsfPath {
    $key = "HKCU:\Software\Classes\CLSID\{13EB305F-2DA3-4CF7-8C45-16B016B801B5}\InprocServer32"
    if (-not (Test-Path -LiteralPath $key)) { return "" }
    return [string](Get-Item -LiteralPath $key).GetValue("")
}

function Get-RuntimeHostPath {
    $key = "HKCU:\Software\PiInput\Runtime"
    if (-not (Test-Path -LiteralPath $key)) { return "" }
    return [string](Get-ItemPropertyValue -LiteralPath $key -Name "CurrentHostPath" -ErrorAction SilentlyContinue)
}

function Test-SamePath([string]$Left, [string]$Right) {
    if ([string]::IsNullOrWhiteSpace($Left) -or [string]::IsNullOrWhiteSpace($Right)) {
        return $false
    }
    return [IO.Path]::GetFullPath($Left).Equals(
        [IO.Path]::GetFullPath($Right), [StringComparison]::OrdinalIgnoreCase)
}

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-ExecutableFromCommandLine([string]$CommandLine) {
    $expanded = [Environment]::ExpandEnvironmentVariables($CommandLine).Trim()
    if ([string]::IsNullOrWhiteSpace($expanded)) { return "" }
    if ($expanded[0] -eq '"') {
        $closingQuote = $expanded.IndexOf('"', 1)
        if ($closingQuote -lt 2) { return "" }
        return $expanded.Substring(1, $closingQuote - 1)
    }
    $firstSpace = $expanded.IndexOf(' ')
    if ($firstSpace -lt 0) { return $expanded }
    return $expanded.Substring(0, $firstSpace)
}

$closureStage = "package-hash"
$actualHash = ""
$buildId = ""
$version = ""
$temp = ""
try {
    $sidecar = "$zip.sha256.txt"
    if (-not (Test-Path -LiteralPath $sidecar -PathType Leaf)) { throw "SHA-256 sidecar is missing: $sidecar" }
    $expectedHash = ((Get-Content -Raw -LiteralPath $sidecar).Trim() -split '\s+')[0].ToLowerInvariant()
    $actualHash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($expectedHash -ne $actualHash) { throw "Package SHA-256 mismatch: expected=$expectedHash actual=$actualHash" }

    $closureStage = "package-layout"
    $temp = Join-Path ([IO.Path]::GetTempPath()) ("PiInput-package-closure-" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Path $temp | Out-Null
    Expand-Archive -LiteralPath $zip -DestinationPath $temp
    $roots = @(Get-ChildItem -LiteralPath $temp -Directory)
    if ($roots.Count -ne 1) { throw "Package must contain exactly one root directory." }
    $package = $roots[0].FullName
    foreach ($required in @(
        "PiInput-Install.exe", "PiInput-Uninstall.exe", "PiInput-Test.exe",
        "bin/PiInputTSF.dll", "bin/PiInputHost.exe", "bin/PiInput-Settings.exe",
        "bin/piinput-diagnostics.exe", "bin/piinput-profile.exe",
        "data/piinput-base.lex", "data/host_protocol.json", "LICENSE_NOTICE.md")) {
        if (-not (Test-Path -LiteralPath (Join-Path $package $required) -PathType Leaf)) {
            throw "Required package file is missing: $required"
        }
    }
    $forbiddenExtensions = @(".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".obj", ".lib", ".exp", ".pdb", ".map", ".js.map", ".ps1", ".cmake", ".vcxproj")
    $leaks = @(Get-ChildItem -LiteralPath $package -Recurse -File |
        Where-Object { $forbiddenExtensions -contains $_.Extension.ToLowerInvariant() })
    if ($leaks.Count -gt 0) { throw "Source/build/script files leaked into package: $($leaks.FullName -join ', ')" }

    $closureStage = "package-identity"
    $hostExe = Join-Path $package "bin/PiInputHost.exe"
    $version = (& $hostExe --version | Select-Object -First 1).Trim()
    $buildId = (& $hostExe --build-id | Select-Object -First 1).Trim()
    $buildIdMatches = if ([string]::IsNullOrWhiteSpace($ExpectedBuildId)) {
        $buildId -match "^$([regex]::Escape($ExpectedVersion))\+.+"
    } else {
        $buildId -eq $ExpectedBuildId
    }
    if ($version -ne $ExpectedVersion -or -not $buildIdMatches) {
        throw "Packaged Host identity mismatch: version=$version build_id=$buildId"
    }

    $closureStage = "package-signatures"
    $peFiles = @(Get-ChildItem -LiteralPath $package -Recurse -File |
        Where-Object { $_.Extension -in @(".exe", ".dll") })
    $signatureRows = foreach ($file in $peFiles) {
        $signature = Get-AuthenticodeSignature -LiteralPath $file.FullName
        $subject = if ($null -ne $signature.SignerCertificate) {
            [string]$signature.SignerCertificate.Subject
        } else {
            ""
        }
        $timestampSubject = if ($null -ne $signature.TimeStamperCertificate) {
            [string]$signature.TimeStamperCertificate.Subject
        } else {
            ""
        }
        [pscustomobject]@{
            File = $file.FullName.Substring($package.Length + 1)
            Status = $signature.Status.ToString()
            Subject = $subject
            TimestampSubject = $timestampSubject
            Sha256 = Get-Sha256 $file.FullName
        }
    }
    $signatureRows | Export-Csv -LiteralPath (Join-Path $ReportDirectory "signatures.csv") -NoTypeInformation -Encoding UTF8
    if ($RequireSigned -and @($signatureRows | Where-Object Status -ne "Valid").Count -gt 0) {
        throw "One or more packaged PE files do not have a valid Authenticode signature."
    }
    if ($RequireSigned -and @($signatureRows | Where-Object { [string]::IsNullOrWhiteSpace($_.TimestampSubject) }).Count -gt 0) {
        throw "One or more packaged PE files do not have an RFC 3161 timestamp certificate."
    }

    $installState = "not-run"
    $installedEvidence = $null
    $controlledTsfState = "not-run"
    $upgradeState = "not-requested"
    $previousVersion = ""
    if ($InstallSmoke) {
        $closureStage = "install-precondition"
        $uninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\PiInput"
        $hadExistingInstall = Test-Path -LiteralPath $uninstallKey
        if ($hadExistingInstall -and -not $AllowExistingInstall) {
            throw "Install smoke requires a clean user profile; an existing PiInput installation was found."
        }
        if ($hadExistingInstall -and -not [string]::IsNullOrWhiteSpace($PreviousPackageZip)) {
            throw "Previous-package upgrade smoke requires a clean user profile."
        }
        function Assert-InstalledState([string]$Label) {
            $entry = Get-ItemProperty -LiteralPath $uninstallKey
            if ([string]$entry.DisplayVersion -ne $ExpectedVersion) {
                throw "Installed DisplayVersion mismatch during $Label."
            }
            $installedRoot = [IO.Path]::GetFullPath([string]$entry.InstallLocation)
            $installedBin = Join-Path $installedRoot "bin"
            $installedHost = Join-Path $installedBin "PiInputHost.exe"
            $installedTsf = Join-Path $installedBin "PiInputTSF.dll"
            foreach ($requiredInstalled in @($installedHost, $installedTsf)) {
                if (-not (Test-Path -LiteralPath $requiredInstalled -PathType Leaf)) {
                    throw "Installed binary is missing during ${Label}: $requiredInstalled"
                }
            }
            $installedVersion = (& $installedHost --version | Select-Object -First 1).Trim()
            $installedBuildId = (& $installedHost --build-id | Select-Object -First 1).Trim()
            if ($installedVersion -ne $ExpectedVersion -or $installedBuildId -ne $buildId) {
                throw "Installed Host identity mismatch during ${Label}: version=$installedVersion build_id=$installedBuildId"
            }
            $registeredTsf = Get-RegisteredTsfPath
            if (-not (Test-SamePath $registeredTsf $installedTsf)) {
                throw "Registered TSF path mismatch during ${Label}: expected=$installedTsf actual=$registeredTsf"
            }
            $runtimeHost = Get-RuntimeHostPath
            if (-not (Test-SamePath $runtimeHost $installedHost)) {
                throw "Registered Host path mismatch during ${Label}: expected=$installedHost actual=$runtimeHost"
            }
            $packageHostHash = Get-Sha256 (Join-Path $package "bin/PiInputHost.exe")
            $packageTsfHash = Get-Sha256 (Join-Path $package "bin/PiInputTSF.dll")
            $installedHostHash = Get-Sha256 $installedHost
            $installedTsfHash = Get-Sha256 $installedTsf
            if ($packageHostHash -ne $installedHostHash -or $packageTsfHash -ne $installedTsfHash) {
                throw "Installed PE hash mismatch during $Label."
            }
            return [pscustomobject]@{
                label = $Label
                install_root = $installedRoot
                host_path = $installedHost
                host_sha256 = $installedHostHash
                tsf_path = $installedTsf
                tsf_sha256 = $installedTsfHash
                registered_tsf_path = $registeredTsf
                registered_host_path = $runtimeHost
                version = $installedVersion
                build_id = $installedBuildId
            }
        }

        $installer = Join-Path $package "PiInput-Install.exe"
        $preservationSentinel = ""
        if (-not [string]::IsNullOrWhiteSpace($PreviousPackageZip)) {
            $closureStage = "previous-package-hash"
            $previousZip = [IO.Path]::GetFullPath($PreviousPackageZip)
            if (-not (Test-Path -LiteralPath $previousZip -PathType Leaf)) {
                throw "Previous package is missing: $previousZip"
            }
            $previousSidecar = "$previousZip.sha256.txt"
            if (-not (Test-Path -LiteralPath $previousSidecar -PathType Leaf)) {
                throw "Previous package SHA-256 sidecar is missing: $previousSidecar"
            }
            $previousExpectedHash = ((Get-Content -Raw -LiteralPath $previousSidecar).Trim() -split '\s+')[0].ToLowerInvariant()
            $previousActualHash = Get-Sha256 $previousZip
            if ($previousExpectedHash -ne $previousActualHash) {
                throw "Previous package SHA-256 mismatch: expected=$previousExpectedHash actual=$previousActualHash"
            }
            $closureStage = "previous-package-install"
            $previousDirectory = Join-Path $temp "previous"
            New-Item -ItemType Directory -Path $previousDirectory | Out-Null
            Expand-Archive -LiteralPath $previousZip -DestinationPath $previousDirectory
            $previousRoots = @(Get-ChildItem -LiteralPath $previousDirectory -Directory)
            if ($previousRoots.Count -ne 1) {
                throw "Previous package must contain exactly one root directory."
            }
            $previousInstaller = Join-Path $previousRoots[0].FullName "PiInput-Install.exe"
            if (-not (Test-Path -LiteralPath $previousInstaller -PathType Leaf)) {
                throw "Previous package installer is missing."
            }
            $previousProcess = Start-Process -FilePath $previousInstaller -ArgumentList "--silent" -Wait -PassThru
            if ($previousProcess.ExitCode -ne 0) {
                throw "Previous package installer failed with exit code $($previousProcess.ExitCode)."
            }
            $previousEntry = Get-ItemProperty -LiteralPath $uninstallKey
            $previousVersion = [string]$previousEntry.DisplayVersion
            if ([string]::IsNullOrWhiteSpace($previousVersion) -or $previousVersion -eq $ExpectedVersion) {
                throw "Previous package version is not a distinct upgrade baseline: $previousVersion"
            }
            $preservationSentinel = Join-Path ([string]$previousEntry.InstallLocation) "UserData/package-closure-preservation.txt"
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $preservationSentinel) | Out-Null
            Set-Content -LiteralPath $preservationSentinel -Encoding ASCII -Value "preserve-across-upgrade"
            $upgradeState = "previous-installed"
        }
        foreach ($pass in 1..2) {
            $closureStage = "install-pass-$pass"
            $process = Start-Process -FilePath $installer -ArgumentList "--silent" -Wait -PassThru
            if ($process.ExitCode -ne 0) { throw "Installer pass $pass failed with exit code $($process.ExitCode)." }
            $installedEvidence = Assert-InstalledState "install-pass-$pass"
            if (-not [string]::IsNullOrWhiteSpace($preservationSentinel) -and
                -not (Test-Path -LiteralPath $preservationSentinel -PathType Leaf)) {
                throw "User data preservation sentinel was lost during install pass $pass."
            }
        }
        if ($upgradeState -eq "previous-installed") { $upgradeState = "passed" }
        $entry = Get-ItemProperty -LiteralPath $uninstallKey
        $installedRoot = [string]$entry.InstallLocation
        $installedDiagnostics = Join-Path $installedRoot "bin/piinput-diagnostics.exe"
        if (Test-Path -LiteralPath $installedDiagnostics -PathType Leaf) {
            & $installedDiagnostics *> (Join-Path $ReportDirectory "installed-diagnostics.txt")
        }
        if (-not [string]::IsNullOrWhiteSpace($ControlledTsfController)) {
            $closureStage = "controlled-tsf-smoke"
            $controller = [IO.Path]::GetFullPath($ControlledTsfController)
            if (-not (Test-Path -LiteralPath $controller -PathType Leaf)) {
                throw "Controlled TSF controller is missing: $controller"
            }
            $controllerOutput = & $controller --piinput-smoke --expected-tsf $installedEvidence.tsf_path 2>&1
            $controllerExitCode = $LASTEXITCODE
            $controllerOutput | Set-Content -LiteralPath (Join-Path $ReportDirectory "controlled-tsf-smoke.txt") -Encoding UTF8
            if ($controllerExitCode -ne 0) {
                throw "Controlled TSF physical-input smoke failed with exit code $controllerExitCode."
            }
            $controlledTsfState = "passed"
        }
        $installedUninstaller = Get-ExecutableFromCommandLine ([string]$entry.QuietUninstallString)
        if (-not (Test-Path -LiteralPath $installedUninstaller -PathType Leaf)) {
            $installedUninstaller = Join-Path $env:LOCALAPPDATA "PiInput/Uninstall/PiInput-Uninstall.exe"
        }
        $uninstallArguments = if ($hadExistingInstall) { "--silent" } else { "--silent --remove-user-data" }
        $closureStage = "silent-uninstall"
        $uninstall = Start-Process -FilePath $installedUninstaller -ArgumentList $uninstallArguments -Wait -PassThru
        if ($uninstall.ExitCode -ne 0) { throw "Uninstaller launcher failed with exit code $($uninstall.ExitCode)." }
        $removed = $false
        foreach ($attempt in 1..100) {
            if (-not (Test-Path -LiteralPath $uninstallKey)) { $removed = $true; break }
            Start-Sleep -Milliseconds 100
        }
        if (-not $removed) { throw "Uninstall registry entry remained after the silent uninstall." }
        if (-not [string]::IsNullOrWhiteSpace((Get-RegisteredTsfPath))) {
            throw "TSF CLSID registration remained after the silent uninstall."
        }
        if (-not [string]::IsNullOrWhiteSpace((Get-RuntimeHostPath))) {
            throw "Host runtime registration remained after the silent uninstall."
        }
        if ($hadExistingInstall -or $LeaveInstalled) {
            $closureStage = "final-reinstall"
            $finalInstall = Start-Process -FilePath $installer -ArgumentList "--silent" -Wait -PassThru
            if ($finalInstall.ExitCode -ne 0) { throw "Final reinstall failed with exit code $($finalInstall.ExitCode)." }
            $installedEvidence = Assert-InstalledState "final-reinstall"
            $installState = if ($upgradeState -eq "passed") {
                "previous-upgrade-reinstall-uninstall-final-reinstall-passed"
            } else {
                "install-reinstall-uninstall-final-reinstall-passed"
            }
        } else {
            $installState = if ($upgradeState -eq "passed") {
                "previous-upgrade-reinstall-uninstall-passed"
            } else {
                "install-reinstall-uninstall-passed"
            }
        }
    }

    $summary = [ordered]@{
        status = "passed"
        stage = "complete"
        package = $zip
        sha256 = $actualHash
        version = $version
        build_id = $buildId
        file_count = @(Get-ChildItem -LiteralPath $package -Recurse -File).Count
        pe_file_count = $peFiles.Count
        signed_required = [bool]$RequireSigned
        install_closure = $installState
        upgrade_from_version = $previousVersion
        upgrade_smoke = $upgradeState
        controlled_tsf_smoke = $controlledTsfState
        installed_identity = $installedEvidence
    }
    $summary | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $ReportDirectory "summary.json") -Encoding UTF8
    Write-Host "Package closure passed: SHA-256, payload, identity, signatures policy, $installState" -ForegroundColor Green
} catch {
    $failureSummary = [ordered]@{
        status = "failed"
        stage = $closureStage
        error = $_.Exception.Message
        package = $zip
        sha256 = $actualHash
        expected_version = $ExpectedVersion
        expected_build_id = $ExpectedBuildId
        observed_version = $version
        observed_build_id = $buildId
        signed_required = [bool]$RequireSigned
        install_smoke_requested = [bool]$InstallSmoke
        previous_package_requested = -not [string]::IsNullOrWhiteSpace($PreviousPackageZip)
    }
    $failureSummary | ConvertTo-Json |
        Set-Content -LiteralPath (Join-Path $ReportDirectory "summary.json") -Encoding UTF8
    throw
} finally {
    if (-not [string]::IsNullOrWhiteSpace($temp) -and (Test-Path -LiteralPath $temp)) {
        Remove-Item -LiteralPath $temp -Recurse -Force
    }
}
