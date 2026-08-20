param(
    [Parameter(Mandatory = $true)][string]$SourceDir
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$generator = Join-Path $SourceDir "scripts/generate-human-input-scenario.ps1"
if (-not (Test-Path -LiteralPath $generator -PathType Leaf)) {
    throw "Missing human input scenario generator: $generator"
}

$fixture = Join-Path ([IO.Path]::GetTempPath()) ("piinput-human-scenario-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force $fixture | Out-Null
try {
    $first = Join-Path $fixture "first"
    $second = Join-Path $fixture "second"
    $randomRun = Join-Path $fixture "random"
    & $generator -Seed 20260815 -OutputDirectory $first
    & $generator -Seed 20260815 -OutputDirectory $second
    & $generator -OutputDirectory $randomRun

    $firstJson = Join-Path $first "human-input-scenario.json"
    $secondJson = Join-Path $second "human-input-scenario.json"
    $firstMarkdown = Join-Path $first "human-input-scenario.md"
    $randomJson = Join-Path $randomRun "human-input-scenario.json"
    foreach ($path in @($firstJson, $secondJson, $firstMarkdown, $randomJson)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Scenario generator did not create: $path"
        }
    }

    function Get-Sha256([string]$Path) {
        $sha = [Security.Cryptography.SHA256]::Create()
        $stream = [IO.File]::OpenRead($Path)
        try {
            return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '')
        } finally {
            $stream.Dispose()
            $sha.Dispose()
        }
    }
    $firstHash = Get-Sha256 $firstJson
    $secondHash = Get-Sha256 $secondJson
    if ($firstHash -ne $secondHash) {
        throw "The same seed must generate an identical scenario."
    }

    $scenario = Get-Content -Raw -LiteralPath $firstJson -Encoding UTF8 | ConvertFrom-Json
    $randomScenario = Get-Content -Raw -LiteralPath $randomJson -Encoding UTF8 | ConvertFrom-Json
    if ($scenario.seed -ne 20260815) { throw "Scenario did not record its seed." }
    if ([int64]$randomScenario.seed -le 0) { throw "Default generation did not create a positive random seed." }
    if (@($scenario.actions).Count -lt 18) { throw "Scenario is too short to model real typing." }

    $types = @($scenario.actions | ForEach-Object type | Sort-Object -Unique)
    foreach ($required in @(
        "type_chinese", "type_english", "newline", "move_left", "move_right",
        "move_up", "move_down", "paste", "backspace", "delete", "shift_toggle",
        "symbol_command"
    )) {
        if ($types -notcontains $required) {
            throw "Scenario is missing required action: $required"
        }
    }

    $lengths = @($scenario.actions |
        Where-Object { $_.type -in @("type_chinese", "type_english") } |
        ForEach-Object { [int]$_.text.Length } |
        Sort-Object -Unique)
    if ($lengths.Count -lt 4) { throw "Scenario typing lengths are not varied enough." }

    foreach ($group in @(1, 2, 3)) {
        if (@($scenario.actions | Where-Object source_group -eq $group).Count -eq 0) {
            throw "Scenario did not sample test text group $group."
        }
    }

    $commands = @($scenario.actions | Where-Object type -eq "symbol_command" | ForEach-Object text)
    if ($commands -notcontains ";;f") { throw "Scenario is missing the semicolon symbol command." }
    if ($commands -notcontains "````f") { throw "Scenario must preserve the two-backtick symbol command." }

    foreach ($action in @($scenario.actions | Where-Object { $_.type -in @("type_chinese", "type_english") })) {
        $sourceLines = Get-Content -LiteralPath (Join-Path $SourceDir "tests/data/real_world_text_corpus.txt") -Encoding UTF8
        if (-not @($sourceLines | Where-Object { $_.StartsWith([string]$action.text) }).Count) {
            throw "Typed fragments must begin at a real sentence boundary: $($action.text)"
        }
    }
} finally {
    Remove-Item -LiteralPath $fixture -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "Human input scenario regression passed."
