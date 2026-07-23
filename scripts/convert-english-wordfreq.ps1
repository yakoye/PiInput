param(
    [Parameter(Mandatory = $true)][string]$InputJson,
    [Parameter(Mandatory = $true)][string]$OutputTsv,
    [string]$RuntimeTsv = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$DownloadedFlag = 2

function Write-AtomicUtf8Lines {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)]$Lines
    )
    $parent = Split-Path -Parent $Path
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Force $parent | Out-Null
    }
    $temporary = "$Path.tmp"
    Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
    try {
        $encoding = [System.Text.UTF8Encoding]::new($false)
        [System.IO.File]::WriteAllLines($temporary, $Lines, $encoding)
        if (Test-Path -LiteralPath $Path) {
            [System.IO.File]::Replace($temporary, $Path, $null)
        } else {
            [System.IO.File]::Move($temporary, $Path)
        }
    } catch {
        Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
        throw
    }
}

$parsed = Get-Content -Raw -LiteralPath $InputJson -Encoding UTF8 | ConvertFrom-Json
$words = [System.Collections.Generic.List[string]]::new()
$seen = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::Ordinal)

foreach ($entry in @($parsed)) {
    if ($null -eq $entry -or $entry.Count -lt 2) {
        continue
    }
    $word = [string]$entry[0]
    $score = 0.0
    $scoreIsNumber = [double]::TryParse(
        [string]$entry[1],
        [System.Globalization.NumberStyles]::Float,
        [System.Globalization.CultureInfo]::InvariantCulture,
        [ref]$score)
    if (-not $scoreIsNumber -or $word -cnotmatch '^[A-Za-z]+$' -or
        -not $seen.Add($word)) {
        continue
    }
    $words.Add($word)
}

if ($words.Count -eq 0) {
    throw "The English wordfreq JSON contained no valid ASCII words."
}

$lines = [System.Collections.Generic.List[string]]::new()
for ($index = 0; $index -lt $words.Count; ++$index) {
    $weight = $words.Count - $index
    $lines.Add("$($words[$index])`t$weight`t$DownloadedFlag")
}
Write-AtomicUtf8Lines -Path $OutputTsv -Lines $lines
if (-not [string]::IsNullOrWhiteSpace($RuntimeTsv)) {
    Write-AtomicUtf8Lines -Path $RuntimeTsv -Lines $lines
}
