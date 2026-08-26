param([Parameter(Mandatory = $true)][string]$SourceDir)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
. (Join-Path $SourceDir "scripts/native-command.ps1")

$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) (
    "piinput-native-capture-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force $fixtureRoot | Out-Null
try {
    $fixture = Join-Path $fixtureRoot "unsupported-scel.cmd"
    [IO.File]::WriteAllText(
        $fixture,
        "@echo off`r`necho Error: Unsupported SCEL fixture 1>&2`r`nexit /b 7`r`n",
        [Text.ASCIIEncoding]::new())

    $result = Invoke-NativeCaptured -FilePath $fixture -ArgumentList @("中文路径.scel")
    if ($result.ExitCode -ne 7) {
        throw "Expected exit code 7, got $($result.ExitCode)."
    }
    if (($result.Output -join " ") -notmatch "Unsupported SCEL fixture") {
        throw "Native stderr was not captured as ordinary diagnostic output."
    }
} finally {
    Remove-Item -LiteralPath $fixtureRoot -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "Native command capture regression passed."
