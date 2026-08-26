param(
    [Parameter(Mandatory = $true)][string]$InstallerExe,
    [Parameter(Mandatory = $true)][string]$UninstallerExe
)

$ErrorActionPreference = "Stop"

function Resolve-ManifestTool {
    $command = Get-Command mt.exe -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }
    $kits = "C:\Program Files (x86)\Windows Kits\10\bin"
    if (Test-Path -LiteralPath $kits) {
        $candidate = Get-ChildItem -LiteralPath $kits -Filter mt.exe -Recurse |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($null -ne $candidate) {
            return $candidate.FullName
        }
    }
    throw "Windows SDK mt.exe was not found."
}

function Assert-AsInvokerManifest {
    param(
        [Parameter(Mandatory = $true)][string]$ManifestTool,
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string]$Output
    )
    if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
        throw "Executable is missing: $Executable"
    }
    & $ManifestTool "-inputresource:$Executable;#1" "-out:$Output"
    if ($LASTEXITCODE -ne 0) {
        throw "mt.exe could not extract the manifest from $Executable"
    }
    $manifest = Get-Content -LiteralPath $Output -Raw
    if ($manifest -notmatch 'requestedExecutionLevel\s+level="asInvoker"\s+uiAccess="false"') {
        throw "Executable does not declare asInvoker: $Executable"
    }
    if ($manifest -match 'requireAdministrator|highestAvailable') {
        throw "Per-user executable unexpectedly requests elevation: $Executable"
    }
}

$temporary = Join-Path ([System.IO.Path]::GetTempPath()) (
    "piinput-uac-manifest-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $temporary | Out-Null
try {
    $mt = Resolve-ManifestTool
    Assert-AsInvokerManifest -ManifestTool $mt -Executable $InstallerExe `
        -Output (Join-Path $temporary "install.manifest")
    Assert-AsInvokerManifest -ManifestTool $mt -Executable $UninstallerExe `
        -Output (Join-Path $temporary "uninstall.manifest")
    Write-Host "Installer and uninstaller manifests are per-user asInvoker."
} finally {
    Remove-Item -LiteralPath $temporary -Recurse -Force -ErrorAction SilentlyContinue
}
