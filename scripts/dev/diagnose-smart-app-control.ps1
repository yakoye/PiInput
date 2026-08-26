# 诊断智能应用控制为何拦下 PiInput 安装器。
#
# 只读脚本：不修改任何设置，不写注册表，不安装任何东西。
# 在被拦截的机器上运行，把输出全部回传即可。
#
# 用法（在 PowerShell 里）：
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\diagnose-smart-app-control.ps1

$ErrorActionPreference = "Continue"

Write-Host "=== PiInput 安装拦截诊断 ===" -ForegroundColor Cyan
Write-Host ""

Write-Host "--- 1. 系统版本 ---"
$os = Get-CimInstance Win32_OperatingSystem
Write-Host "  $($os.Caption)  Build $($os.BuildNumber)"
try {
    $display = (Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion" -ErrorAction Stop).DisplayVersion
    Write-Host "  版本代号: $display"
} catch { }
Write-Host ""

Write-Host "--- 2. 智能应用控制状态 ---"
try {
    $state = (Get-ItemProperty "HKLM:\SYSTEM\CurrentControlSet\Control\CI\Policy" `
        -Name VerifiedAndReputablePolicyState -ErrorAction Stop).VerifiedAndReputablePolicyState
    $label = switch ($state) {
        0 { "关闭——不会拦截任何应用" }
        1 { "强制模式——拦截未签名且无正面判定的应用，且不提供放行入口" }
        2 { "评估模式——只观察不拦截" }
        default { "未知值 $state" }
    }
    Write-Host "  VerifiedAndReputablePolicyState = $state（$label）"
} catch {
    Write-Host "  未查到该策略键，可能是不支持智能应用控制的 Windows 版本"
}
Write-Host ""

Write-Host "--- 3. 代码完整性拦截记录（最近 30 条）---"
Write-Host "  这一节最关键：它会写明到底哪个文件被拦、依据哪条策略。"
$found = $false
foreach ($logName in @(
    "Microsoft-Windows-CodeIntegrity/Operational",
    "Microsoft-Windows-AppLocker/MSI and Script",
    "Microsoft-Windows-Security-Mitigations/KernelMode")) {
    try {
        $events = Get-WinEvent -LogName $logName -MaxEvents 30 -ErrorAction Stop |
            Where-Object { $_.Message -match "PiInput|blocked|阻止" }
        if ($events) {
            $found = $true
            Write-Host "  [$logName]" -ForegroundColor Yellow
            foreach ($e in $events) {
                Write-Host "    $($e.TimeCreated)  Id=$($e.Id)  $($e.LevelDisplayName)"
                $text = ($e.Message -split "`r?`n" | Where-Object { $_.Trim() -ne "" }) -join " | "
                if ($text.Length -gt 400) { $text = $text.Substring(0, 400) + "…" }
                Write-Host "      $text"
            }
        }
    } catch { }
}
if (-not $found) {
    Write-Host "  没有找到与 PiInput 相关的拦截事件。"
    Write-Host "  若确实被拦过，请先重现一次拦截再立刻运行本脚本。"
}
Write-Host ""

Write-Host "--- 4. 安装器文件状态 ---"
$candidates = @()
foreach ($base in @("$env:USERPROFILE\Documents", "$env:USERPROFILE\Downloads", "$env:USERPROFILE\Desktop")) {
    if (Test-Path $base) {
        $candidates += Get-ChildItem $base -Recurse -Filter "PiInput-Install.exe" -ErrorAction SilentlyContinue
    }
}
if ($candidates.Count -eq 0) {
    Write-Host "  未在文档/下载/桌面下找到 PiInput-Install.exe"
} else {
    foreach ($exe in ($candidates | Select-Object -First 5)) {
        Write-Host "  $($exe.FullName)"
        $sig = Get-AuthenticodeSignature $exe.FullName
        Write-Host "    签名: $($sig.Status)"
        Write-Host "    SHA-256: $((Get-FileHash $exe.FullName -Algorithm SHA256).Hash.ToLowerInvariant())"
        # 从别处下载来的文件带 Zone.Identifier 标记，会额外触发 SmartScreen。
        $zone = Get-Item $exe.FullName -Stream Zone.Identifier -ErrorAction SilentlyContinue
        if ($zone) {
            Write-Host "    含来源标记（Zone.Identifier）——这会额外触发 SmartScreen" -ForegroundColor Yellow
        } else {
            Write-Host "    无来源标记"
        }
    }
}
Write-Host ""

Write-Host "--- 5. SmartScreen 设置 ---"
try {
    $ss = (Get-ItemProperty "HKLM:\SOFTWARE\Policies\Microsoft\Windows\System" `
        -Name EnableSmartScreen -ErrorAction Stop).EnableSmartScreen
    Write-Host "  策略 EnableSmartScreen = $ss"
} catch {
    Write-Host "  未配置组策略，使用系统默认"
}
Write-Host ""
Write-Host "=== 诊断结束，请把以上全部内容回传 ===" -ForegroundColor Cyan
