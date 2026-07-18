param([string]$Version = "")
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($Version)) { $Version = (Get-Content (Join-Path $Root "VERSION") -Raw).Trim() }
$Parent = Split-Path -Parent $Root
$ZipName = "lite-ime-v$Version.zip"
$Zip = Join-Path $Parent $ZipName
$StageRoot = Join-Path $Parent ".liteime-release-stage"
$Stage = Join-Path $StageRoot "lite-ime-dev"
if (Test-Path $StageRoot) { Remove-Item $StageRoot -Recurse -Force }
if (Test-Path $Zip) { Remove-Item $Zip -Force }
New-Item $StageRoot -ItemType Directory -Force | Out-Null
Copy-Item $Root $Stage -Recurse
Get-ChildItem $Stage -Directory -Recurse -Force |
    Where-Object { $_.Name -in @("build", "dist", ".git", ".vs", "__pycache__") } |
    Sort-Object FullName -Descending |
    Remove-Item -Recurse -Force
Get-ChildItem $Stage -File -Recurse -Force |
    Where-Object { $_.Extension -in @(".obj", ".pdb", ".ilk", ".exe", ".dll", ".lib") } |
    Remove-Item -Force
Compress-Archive -Path $Stage -DestinationPath $Zip -CompressionLevel Optimal
Remove-Item $StageRoot -Recurse -Force
Write-Host $Zip -ForegroundColor Green
