param(
    [Parameter(Mandatory = $true)][string]$InputJson,
    [Parameter(Mandatory = $true)][string]$OutputTsv,
    [string]$RuntimeTsv = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$DownloadedFlag = 2

function New-TransactionFileName {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Kind
    )
    return "$Path.$Kind.$PID.$([Guid]::NewGuid().ToString('N'))"
}

function Write-Utf8Lines {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)]$Lines
    )
    $parent = Split-Path -Parent $Path
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Force $parent | Out-Null
    }
    $encoding = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllLines($Path, $Lines, $encoding)
}

function Publish-TransactionalUtf8Lines {
    param(
        [Parameter(Mandatory = $true)][string[]]$Paths,
        [Parameter(Mandatory = $true)]$Lines
    )

    $items = @()
    try {
        foreach ($path in $Paths) {
            if ([string]::IsNullOrWhiteSpace($path)) { continue }
            $temporary = New-TransactionFileName -Path $path -Kind "tmp"
            $backup = New-TransactionFileName -Path $path -Kind "bak"
            Write-Utf8Lines -Path $temporary -Lines $Lines
            $items += [pscustomobject]@{
                Path = $path
                Temporary = $temporary
                Backup = $backup
                Existed = [System.IO.File]::Exists($path)
                Published = $false
            }
        }

        foreach ($item in $items) {
            if ($item.Existed) {
                # A non-empty backup path is required by Windows PowerShell 5.1.
                [System.IO.File]::Replace($item.Temporary, $item.Path, $item.Backup)
            } else {
                [System.IO.File]::Move($item.Temporary, $item.Path)
            }
            $item.Published = $true
        }
    } catch {
        for ($index = $items.Count - 1; $index -ge 0; --$index) {
            $item = $items[$index]
            if (-not $item.Published) { continue }
            try {
                if ($item.Existed -and [System.IO.File]::Exists($item.Backup)) {
                    $rollback = New-TransactionFileName -Path $item.Path -Kind "rollback"
                    [System.IO.File]::Replace($item.Backup, $item.Path, $rollback)
                    [System.IO.File]::Delete($rollback)
                } elseif (-not $item.Existed -and [System.IO.File]::Exists($item.Path)) {
                    [System.IO.File]::Delete($item.Path)
                }
            } catch {
                throw "English dictionary publish failed and rollback could not restore '$($item.Path)': $($_.Exception.Message)"
            }
        }
        throw
    } finally {
        foreach ($item in $items) {
            foreach ($artifact in @($item.Temporary, $item.Backup)) {
                if ([System.IO.File]::Exists($artifact)) {
                    [System.IO.File]::Delete($artifact)
                }
            }
        }
    }
}

$parsed = Get-Content -Raw -LiteralPath $InputJson -Encoding UTF8 | ConvertFrom-Json
$entries = [System.Collections.ArrayList]::new()
if ($parsed -is [System.Array] -and $parsed.Count -ge 2 -and
    $parsed[0] -is [string] -and -not ($parsed[1] -is [System.Array])) {
    [void]$entries.Add($parsed)
} else {
    foreach ($parsedEntry in @($parsed)) { [void]$entries.Add($parsedEntry) }
}
$words = [System.Collections.Generic.List[string]]::new()
$seen = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::Ordinal)

foreach ($entry in $entries) {
    if ($null -eq $entry -or -not ($entry -is [System.Array]) -or
        @($entry).Count -lt 2) {
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
$targets = @($OutputTsv)
if (-not [string]::IsNullOrWhiteSpace($RuntimeTsv)) { $targets += $RuntimeTsv }
Publish-TransactionalUtf8Lines -Paths $targets -Lines $lines
