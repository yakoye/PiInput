param(
    [Parameter(Mandatory = $true)]
    [string]$HostExe,
    [Parameter(Mandatory = $true)]
    [string]$ClientExe,
    [string]$LexiconPath = "",
    [string]$DataDir = ""
)

$ErrorActionPreference = "Stop"
$server = $null
$previousInstance = $env:PIINPUT_HOST_INSTANCE
$previousPackageData = $env:PIINPUT_PACKAGE_DATA_DIR
$previousUserData = $env:PIINPUT_USER_DATA_DIR
$runtimeFixture = $null
$env:PIINPUT_HOST_INSTANCE = "ctest_process_$PID"
try {
    if (-not (Test-Path -LiteralPath $HostExe -PathType Leaf)) {
        throw "PiInputHost.exe is missing: $HostExe"
    }
    if (-not (Test-Path -LiteralPath $ClientExe -PathType Leaf)) {
        throw "PiInput Host fixture client is missing: $ClientExe"
    }
    if (-not [string]::IsNullOrWhiteSpace($LexiconPath)) {
        if (-not (Test-Path -LiteralPath $LexiconPath -PathType Leaf)) {
            throw "External performance lexicon is missing: $LexiconPath"
        }
        if (-not (Test-Path -LiteralPath $DataDir -PathType Container)) {
            throw "Packaged data fixture is missing: $DataDir"
        }
        $runtimeFixture = Join-Path ([IO.Path]::GetTempPath()) ("piinput-host-perf-" + [guid]::NewGuid().ToString("N"))
        $fixtureData = Join-Path $runtimeFixture "data"
        $fixtureUser = Join-Path $runtimeFixture "user"
        New-Item -ItemType Directory -Path $fixtureData, $fixtureUser -Force | Out-Null
        Copy-Item -LiteralPath $LexiconPath -Destination (Join-Path $fixtureData "piinput-base.lex")
        foreach ($name in @("symbols.tsv", "english_lexicon.tsv", "english_supplement.tsv", "english_completion_preferences.tsv")) {
            $source = Join-Path $DataDir $name
            if (Test-Path -LiteralPath $source -PathType Leaf) {
                Copy-Item -LiteralPath $source -Destination (Join-Path $fixtureData $name)
            }
        }
        $env:PIINPUT_PACKAGE_DATA_DIR = $fixtureData
        $env:PIINPUT_USER_DATA_DIR = $fixtureUser
    }

    $coldTimer = [Diagnostics.Stopwatch]::StartNew()
    $server = Start-Process -FilePath $HostExe -ArgumentList "--serve" -PassThru -WindowStyle Hidden
    $health = $null
    for ($attempt = 0; $attempt -lt 40; $attempt++) {
        $health = & $HostExe --health 2>&1
        if ($LASTEXITCODE -eq 0) { break }
        Start-Sleep -Milliseconds 50
    }
    $coldTimer.Stop()
    if ($LASTEXITCODE -ne 0 -or ($health -join "`n") -notmatch "protocol=3" -or
        ($health -join "`n") -notmatch "startup_ms=[0-9]+" -or
        ($health -join "`n") -notmatch "version=[0-9]+\.[0-9]+\.[0-9]+" -or
        ($health -join "`n") -notmatch "lexicon_storage=(mmap|heap)" -or
        ($health -join "`n") -notmatch "lexicon_mapped_bytes=[0-9]+" -or
        ($health -join "`n") -notmatch "host_pid=[1-9][0-9]*" -or
        ($health -join "`n") -notmatch "build_id=") {
        throw "Host health handshake failed: $($health -join ' ')"
    }
    if (-not [string]::IsNullOrWhiteSpace($LexiconPath) -and
        (($health -join "`n") -notmatch "lexicon_storage=mmap" -or
         ($health -join "`n") -notmatch "lexicon_mapped_bytes=[1-9][0-9]*")) {
        throw "Host did not memory-map the packaged binary lexicon: $($health -join ' ')"
    }
    $buildId = (& $HostExe --build-id 2>&1 | Select-Object -First 1).ToString().Trim()
    $version = (& $HostExe --version 2>&1 | Select-Object -First 1).ToString().Trim()
    if ($version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+$' -or
        $buildId -notmatch "^$([regex]::Escape($version))\+.+") {
        throw "Host did not separate semantic version from exact build ID: version=$version build=$buildId"
    }
    $matchingHealth = & $HostExe --health $buildId 2>&1
    if ($LASTEXITCODE -ne 0 -or ($matchingHealth -join "`n") -notmatch
        "build_id=$([regex]::Escape($buildId))") {
        throw "Host health did not verify the exact running build: $($matchingHealth -join ' ')"
    }
    $mismatchedHealth = & $HostExe --health "$buildId-mismatch" 2>&1
    if ($LASTEXITCODE -eq 0) {
        throw "Host health accepted another running build: $($mismatchedHealth -join ' ')"
    }
    # The Host reports how long it took to become ready. Timing it from here
    # instead measured this script starting four more processes, which on a
    # loaded machine is most of the reading -- the assertion failed while the
    # Host itself was well inside budget, so it was gating on the build agent
    # rather than on the input method.
    $startup = [regex]::Match(($health -join "`n"), 'startup_ms=(\d+)')
    if (-not $startup.Success) {
        throw "Host health did not report its startup duration: $($health -join ' ')"
    }
    if ([int64]$startup.Groups[1].Value -ge 3000) {
        throw "Host reported a cold startup of $($startup.Groups[1].Value) ms, over the 3000 ms budget"
    }

    # Pure round trip with no engine work. Reusing the pipe read buffers instead
    # of allocating the 1 MiB protocol ceiling per message took this from about
    # 0.8 ms to under 0.2 ms, so a 2 ms ceiling still leaves slow machines room
    # while catching a reintroduction of the per-message allocation.
    $transportBurst = & $ClientExe --transport-burst 80 2>&1
    $transportP95 = [regex]::Match(($transportBurst -join "`n"), 'transport_p95_us=(\d+)')
    if ($LASTEXITCODE -ne 0 -or -not $transportP95.Success -or
        [int64]$transportP95.Groups[1].Value -ge 2000) {
        throw "Resident Host transport P95 must stay below 2 ms during continuous typing: $($transportBurst -join ' ')"
    }

    $settingsPath = Join-Path $env:PIINPUT_USER_DATA_DIR "settings.ini"
    New-Item -ItemType Directory -Path (Split-Path -Parent $settingsPath) -Force | Out-Null
    Set-Content -LiteralPath $settingsPath -Encoding UTF8 -Value @"
[general]
hot_reload=true
[candidates]
font_size=18
window_height=48
"@

    $firstTimer = [Diagnostics.Stopwatch]::StartNew()
    $typed = & $ClientExe "wo" 2>&1
    $firstTimer.Stop()
    $firstRequest = [regex]::Match(($typed -join "`n"), 'request_max_us=(\d+)')
    if ($LASTEXITCODE -ne 0 -or ($typed -join "`n") -notmatch "raw=wo" -or
        ($typed -join "`n") -notmatch "first=我" -or -not $firstRequest.Success -or
        [int64]$firstRequest.Groups[1].Value -ge 100000 -or
        $firstTimer.ElapsedMilliseconds -ge 1000) {
        throw "Host key/session dispatch failed: $($typed -join ' ')"
    }

    $punctuationChain = & $ClientExe --punctuation-chain 2>&1
    $punctuationP95 = [regex]::Match(
        ($punctuationChain -join "`n"), 'punctuation_chain_p95_us=(\d+)')
    $punctuationMax = [regex]::Match(
        ($punctuationChain -join "`n"), 'punctuation_chain_max_us=(\d+)')
    if ($LASTEXITCODE -ne 0 -or
        ($punctuationChain -join "`n") -notmatch 'punctuation_chain_text=我，他们。' -or
        -not $punctuationP95.Success -or -not $punctuationMax.Success -or
        [int64]$punctuationP95.Groups[1].Value -ge 6000 -or
        [int64]$punctuationMax.Groups[1].Value -ge 12000) {
        throw "Chinese word-punctuation-word chain failed or exceeded the 6 ms P95 / 12 ms max key limits: $($punctuationChain -join ' ')"
    }

    $matchingCases = @(
        # Long input without one lexical whole phrase offers the longest real
        # prefix word; it no longer exposes a mechanically joined sentence.
        @{ Input = "womfmktm"; Expected = "我们" },
        @{ Input = "qunali"; Expected = "去哪里" },
        @{ Input = "vegeui"; Expected = "这个是" },
        @{ Input = "nijtde"; Expected = "你觉得" },
        @{ Input = "zfyh"; Expected = "怎样" },
        @{ Input = "drlojuzi"; Expected = "段落" },
        @{ Input = "mktm"; Expected = "明天" },
        @{ Input = "xnhe"; Expected = "小河" },
        @{ Input = "yuwh"; Expected = "欲望" },
        @{ Input = "yvwh"; Expected = "欲望" }
    )
    foreach ($case in $matchingCases) {
        $matched = & $ClientExe $case.Input 2>&1
        $requestTime = [regex]::Match(($matched -join "`n"), 'request_max_us=(\d+)')
        $requestP95 = [regex]::Match(($matched -join "`n"), 'request_p95_us=(\d+)')
        if ($LASTEXITCODE -ne 0 -or ($matched -join "`n") -notmatch "first=$([regex]::Escape($case.Expected))" -or
            -not $requestTime.Success -or -not $requestP95.Success -or
            [int64]$requestP95.Groups[1].Value -ge 6000 -or
            [int64]$requestTime.Groups[1].Value -ge 12000) {
            throw "Host matching failed or exceeded the 6 ms P95 / 12 ms max key limits for $($case.Input): $($matched -join ' ')"
        }
    }

    $warmTimer = [Diagnostics.Stopwatch]::StartNew()
    $warm = & $ClientExe "wo" 2>&1
    $warmTimer.Stop()
    $warmRequest = [regex]::Match(($warm -join "`n"), 'request_max_us=(\d+)')
    if ($LASTEXITCODE -ne 0 -or ($warm -join "`n") -notmatch "first=我" -or
        -not $warmRequest.Success -or [int64]$warmRequest.Groups[1].Value -ge 100000 -or
        $warmTimer.ElapsedMilliseconds -ge 1000) {
        throw "Resident Host did not serve the second application promptly ($($warmTimer.ElapsedMilliseconds) ms): $($warm -join ' ')"
    }

    $caret = & $ClientExe --caret "wo" 2>&1
    if ($LASTEXITCODE -ne 0 -or ($caret -join "`n") -notmatch "caret_ack=yes") {
        throw "Host caret dispatch failed: $($caret -join ' ')"
    }

    $toolbar = & $ClientExe --toolbar-responsive "wo" 2>&1
    if ($LASTEXITCODE -ne 0 -or ($toolbar -join "`n") -notmatch "toolbar_ack=yes" -or
        ($toolbar -join "`n") -notmatch "toolbar_symbols=yes") {
        throw "Host candidate toolbar did not process a mouse click while the pipe server was idle: $($toolbar -join ' ')"
    }


    $initialWindow = & $ClientExe --window-height "wo" 2>&1
    $initialHeightMatch = [regex]::Match(($initialWindow -join "`n"), 'window_height=(\d+)')
    if ($LASTEXITCODE -ne 0 -or -not $initialHeightMatch.Success) {
        throw "Initial candidate height could not be observed: $($initialWindow -join ' ')"
    }
    Set-Content -LiteralPath $settingsPath -Encoding UTF8 -Value @"
[general]
hot_reload=true
[candidates]
font_size=24
window_height=72
"@
    $updatedWindow = & $ClientExe --window-height "wo" 2>&1
    $updatedHeightMatch = [regex]::Match(($updatedWindow -join "`n"), 'window_height=(\d+)')
    if ($LASTEXITCODE -ne 0 -or -not $updatedHeightMatch.Success -or
        [int]$updatedHeightMatch.Groups[1].Value -le [int]$initialHeightMatch.Groups[1].Value) {
        throw "Candidate visual settings did not reload at the next composition boundary: initial=$($initialWindow -join ' ') updated=$($updatedWindow -join ' ')"
    }

    Set-Content -LiteralPath $settingsPath -Encoding UTF8 -Value @"
[general]
schema=full
default_language=chinese
hot_reload=true
[candidates]
font_size=18
window_height=20
"@
    $fullPinyin = & $ClientExe "ganjue" 2>&1
    if ($LASTEXITCODE -ne 0 -or ($fullPinyin -join "`n") -notmatch "first=感觉") {
        throw "Full Pinyin setting did not apply at a new composition boundary: $($fullPinyin -join ' ')"
    }

    Set-Content -LiteralPath $settingsPath -Encoding UTF8 -Value @"
[general]
schema=full
default_language=english
hot_reload=true
[candidates]
font_size=18
window_height=20
"@
    $defaultEnglish = & $ClientExe "b" 2>&1
    if ($LASTEXITCODE -ne 0 -or ($defaultEnglish -join "`n") -notmatch "text=b" -or
        ($defaultEnglish -join "`n") -notmatch "mode=1") {
        throw "Default English language did not apply to the next new session: $($defaultEnglish -join ' ')"
    }

    $second = & $HostExe --serve 2>&1
    if ($LASTEXITCODE -ne 2 -or ($second -join "`n") -notmatch "already_running_build_id=") {
        throw "Second Host did not report the first instance: $($second -join ' ')"
    }

    $drain = & $HostExe --drain 2>&1
    if ($LASTEXITCODE -ne 0 -or ($drain -join "`n") -notmatch "draining=yes") {
        throw "Host drain command failed: $($drain -join ' ')"
    }
    if (-not $server.WaitForExit(5000)) {
        throw "Host did not exit after drain"
    }
    if ($server.ExitCode -ne 0) {
        throw "Host server exited with $($server.ExitCode)"
    }
}
finally {
    if ($null -ne $server -and -not $server.HasExited) {
        Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
    }
    $env:PIINPUT_HOST_INSTANCE = $previousInstance
    $env:PIINPUT_PACKAGE_DATA_DIR = $previousPackageData
    $env:PIINPUT_USER_DATA_DIR = $previousUserData
    if ($null -ne $runtimeFixture -and (Test-Path -LiteralPath $runtimeFixture)) {
        Remove-Item -LiteralPath $runtimeFixture -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "PiInput Host process tests passed. cold=$($coldTimer.ElapsedMilliseconds)ms first=$($firstTimer.ElapsedMilliseconds)ms warm=$($warmTimer.ElapsedMilliseconds)ms"
