function Invoke-NativeCaptured {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter()][string[]]$ArgumentList = @()
    )

    $previousPreference = $ErrorActionPreference
    $hadNativePreference = Test-Path Variable:PSNativeCommandUseErrorActionPreference
    if ($hadNativePreference) {
        $previousNativePreference = $PSNativeCommandUseErrorActionPreference
    }
    try {
        # Windows PowerShell 5.1 turns redirected native stderr into an
        # ErrorRecord. A caller using Stop would terminate before it can use
        # the process exit code to classify an unsupported SCEL as skipped.
        $ErrorActionPreference = "Continue"
        if ($hadNativePreference) {
            $PSNativeCommandUseErrorActionPreference = $false
        }
        $captured = @(& $FilePath @ArgumentList 2>&1)
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousPreference
        if ($hadNativePreference) {
            $PSNativeCommandUseErrorActionPreference = $previousNativePreference
        }
    }

    [pscustomobject]@{
        ExitCode = [int]$exitCode
        Output = @($captured | ForEach-Object { $_.ToString() })
    }
}
