param(
    [Parameter(Mandatory = $true)]
    [string]$HostExe,
    [Parameter(Mandatory = $true)]
    [string]$ClientExe,
    [Parameter(Mandatory = $true)]
    [string]$DataDir
)

# 常用语的端到端回归：真实 Host、真实命名管道、真实协议。
#
# host_session 层的单元测试覆盖了判定逻辑，但数字建议是**唯一走协议 v7 的报文**，
# 而 v7 的信封版本、文本载荷和 Host 的解码分支此前只被编解码单测验证过，从没在
# 管道上跑通一次。信封与载荷版本对不上会让整条报文被拒收——那是「这个应用彻底
# 打不出字」的形态，不是能靠单测发现的东西。
#
# 全程使用独立的 PIINPUT_HOST_INSTANCE 和独立数据目录，不碰用户正在使用的输入法。

$ErrorActionPreference = "Stop"
$server = $null
$previousInstance = $env:PIINPUT_HOST_INSTANCE
$previousPackageData = $env:PIINPUT_PACKAGE_DATA_DIR
$previousUserData = $env:PIINPUT_USER_DATA_DIR
$fixtureRoot = $null
$env:PIINPUT_HOST_INSTANCE = "ctest_phrase_$PID"

function Get-Field([string[]]$Lines, [string]$Name) {
    foreach ($line in $Lines) {
        if ($line -match "^$([regex]::Escape($Name))=(.*)$") { return $Matches[1] }
    }
    return $null
}

try {
    foreach ($path in @($HostExe, $ClientExe)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "缺少可执行文件: $path"
        }
    }
    if (-not (Test-Path -LiteralPath $DataDir -PathType Container)) {
        throw "缺少随包数据目录: $DataDir"
    }

    $fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ("piinput-phrase-" + [guid]::NewGuid().ToString("N"))
    $fixtureData = Join-Path $fixtureRoot "data"
    $fixtureUser = Join-Path $fixtureRoot "user"
    New-Item -ItemType Directory -Path $fixtureData, $fixtureUser -Force | Out-Null
    foreach ($name in @("piinput-base.lex", "base_lexicon.tsv", "symbols.tsv",
                        "english_lexicon.tsv", "english_supplement.tsv",
                        "english_completion_preferences.tsv")) {
        $source = Join-Path $DataDir $name
        if (Test-Path -LiteralPath $source -PathType Leaf) {
            Copy-Item -LiteralPath $source -Destination (Join-Path $fixtureData $name)
        }
    }

    # 两条数字常用语，一条留空触发码、一条填了触发码。后者**不应**被数字触发——
    # 留不留触发码同时是默认行为和这个开关，否则存了证件号之后在任何地方打出开头
    # 几位都会把号码弹到屏幕上。
    $settings = @(
        "[phrases]",
        "count=2",
        "aliases_1=",
        "position_1=2",
        "label_1=",
        "text_1=15801616544",
        "aliases_2=sfz",
        "position_2=2",
        "label_2=",
        "text_2=622322199005012219"
    ) -join "`n"
    [System.IO.File]::WriteAllText(
        (Join-Path $fixtureUser "settings.ini"), $settings + "`n",
        [System.Text.UTF8Encoding]::new($false))

    $env:PIINPUT_PACKAGE_DATA_DIR = $fixtureData
    $env:PIINPUT_USER_DATA_DIR = $fixtureUser

    $server = Start-Process -FilePath $HostExe -ArgumentList "--serve" -PassThru -WindowStyle Hidden
    for ($attempt = 0; $attempt -lt 60; $attempt++) {
        & $HostExe --health > $null 2>&1
        if ($LASTEXITCODE -eq 0) { break }
        Start-Sleep -Milliseconds 50
    }
    if ($LASTEXITCODE -ne 0) { throw "Host 没有在预期时间内就绪。" }

    # 1. 三位命中：给出完整号码，且**不带任何合成串**——那串数字已经在文档里了，
    #    再显示一份就是重复。
    $hit = & $ClientExe --digit-run 158 2>&1
    if ($LASTEXITCODE -ne 0) { throw "digit-run 158 失败，退出码 $LASTEXITCODE`n$($hit -join "`n")" }
    if ((Get-Field $hit "digit_run_accepted") -ne "true") {
        throw "三位数字应当给出建议：`n$($hit -join "`n")"
    }
    if ((Get-Field $hit "digit_run_candidates") -ne "1") {
        throw "应当恰好一条建议：`n$($hit -join "`n")"
    }
    if ((Get-Field $hit "digit_run_candidate_0") -ne "15801616544") {
        throw "建议应当显示完整号码：`n$($hit -join "`n")"
    }
    if (-not [string]::IsNullOrEmpty((Get-Field $hit "digit_run_raw"))) {
        throw "建议不得带合成串：`n$($hit -join "`n")"
    }
    # 2. 选中只插入没打完的那段后缀。这一条决定了这个功能在 TSF 支持差的终端里
    #    也能用：不删除已上屏的数字，也不重写周边文本。
    if ((Get-Field $hit "digit_run_commit_text") -ne "01616544") {
        throw "选中应当只提交未打完的后缀：`n$($hit -join "`n")"
    }

    # 3. 低于门槛不出建议。一位两位会频繁误弹——日期、价格、编号里到处是短数字。
    $short = & $ClientExe --digit-run 15 2>&1
    if ($LASTEXITCODE -ne 0) { throw "digit-run 15 失败，退出码 $LASTEXITCODE" }
    if ((Get-Field $short "digit_run_accepted") -ne "false") {
        throw "两位数字不应给出建议：`n$($short -join "`n")"
    }

    # 4. 不匹配的数字串不出建议。
    $miss = & $ClientExe --digit-run 999 2>&1
    if ($LASTEXITCODE -ne 0) { throw "digit-run 999 失败，退出码 $LASTEXITCODE" }
    if ((Get-Field $miss "digit_run_candidates") -ne "0") {
        throw "不匹配的数字串不应给出建议：`n$($miss -join "`n")"
    }

    # 5. 填了触发码的那条，不该被它自己的数字触发。
    $aliased = & $ClientExe --digit-run 622 2>&1
    if ($LASTEXITCODE -ne 0) { throw "digit-run 622 失败，退出码 $LASTEXITCODE" }
    if ((Get-Field $aliased "digit_run_candidates") -ne "0") {
        throw "填了触发码的常用语不应再被数字触发：`n$($aliased -join "`n")"
    }

    # 6. 别名那条路：打 sfz 应当在候选行里出现那条常用语。它走的是普通 text 按键，
    #    与数字建议是两条完全不同的路径。
    #    行里显示的是截短形式而不是完整内容，这是刻意的：一条三十字的地址放进候选
    #    行会把同一行的词挤到不可读（候选窗按列对齐并等比缩放）。完整内容在选中时
    #    才提交，两者故意不同。
    $byAlias = & $ClientExe sfz 2>&1
    if ($LASTEXITCODE -ne 0) { throw "输入 sfz 失败，退出码 $LASTEXITCODE`n$($byAlias -join "`n")" }
    $atPosition2 = Get-Field $byAlias "candidate_1"
    if (-not $atPosition2.StartsWith("62232219900")) {
        throw "别名 sfz 应当把常用语带到候选第 2 位：`n$($byAlias -join "`n")"
    }
    if ($atPosition2 -eq "622322199005012219") {
        throw "候选行应当显示截短形式，而不是整条内容：`n$($byAlias -join "`n")"
    }

    Write-Host "常用语端到端回归通过：别名命中、数字建议只补后缀且不带合成串，门槛与开关均成立。" -ForegroundColor Green
}
finally {
    if ($null -ne $server -and -not $server.HasExited) {
        # --drain 让 Host 自己收尾；收不掉再强杀，免得留下一个占着管道的孤儿进程。
        try { & $HostExe --drain > $null 2>&1 } catch { }
        if (-not $server.WaitForExit(5000)) {
            Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
        }
    }
    $env:PIINPUT_HOST_INSTANCE = $previousInstance
    $env:PIINPUT_PACKAGE_DATA_DIR = $previousPackageData
    $env:PIINPUT_USER_DATA_DIR = $previousUserData
    if ($null -ne $fixtureRoot -and (Test-Path -LiteralPath $fixtureRoot)) {
        Remove-Item -LiteralPath $fixtureRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
