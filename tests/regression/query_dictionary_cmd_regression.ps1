param(
    [Parameter(Mandatory = $true)][string]$SourceDir
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$commandFile = Join-Path $SourceDir "query-dictionary.cmd"
if (-not (Test-Path -LiteralPath $commandFile -PathType Leaf)) {
    throw "Missing query-dictionary.cmd: $commandFile"
}

$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ("piinput-query-cmd-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force $fixtureRoot | Out-Null
try {
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = Join-Path $env:SystemRoot "System32/cmd.exe"
    $startInfo.Arguments = "/d /s /c `"`"$commandFile`"`""
    $startInfo.WorkingDirectory = $SourceDir
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardInput = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.EnvironmentVariables["LOCALAPPDATA"] = $fixtureRoot

    $process = [Diagnostics.Process]::Start($startInfo)
    $process.StandardInput.WriteLine("")
    $process.StandardInput.WriteLine("")
    $process.StandardInput.Close()
    if (-not $process.WaitForExit(15000)) {
        $process.Kill()
        throw "query-dictionary.cmd did not terminate after its prompt input was supplied."
    }

    $standardOutput = $process.StandardOutput.ReadToEnd()
    $standardError = $process.StandardError.ReadToEnd()
    $combined = $standardOutput + "`n" + $standardError
    if ($combined -match "is not recognized as an internal or external command") {
        throw "cmd.exe misparsed query-dictionary.cmd commands:`n$combined"
    }
    if ($combined -match "(?m)^'?(65001|-NoProfile|1|词库查询失败。)'? is not recognized") {
        throw "The first token of a query-dictionary.cmd line was lost:`n$combined"
    }
} finally {
    Remove-Item -LiteralPath $fixtureRoot -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "query-dictionary.cmd parser regression passed."
