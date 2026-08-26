# PiInput v0.8.0 验证记录

验证日期：2026-08-26

<!-- release-gates
host_soak_8h=NOT_RUN
tsf_app_soak_8h=NOT_RUN
p0_real_host_matrix=NOT_RUN
installer_ui_real_run=NOT_RUN
english_completion_real_run=NOT_RUN
-->

## 自动验证

- 干净 Release 构建，全量 CTest 通过（68 项，含本版新增的 `piinput-english-completion`）。
- `piinput-keystroke-latency` 与 `piinput-typing-latency` 均未退化。英文前缀查询位于按键路径上，词库从 24,323 词扩到约 25 万词后仍未触发延迟门禁。
- 新增单元测试覆盖：位置查表六个区间、双拼与全拼阈值差异、触发条件（单字母、含数字、开关关闭、无匹配前缀）、大小写三种形态、快捷命令不被挤占、词库三层分数不重叠、`palladium` 可在三个候选内被补出。
- `host_session` 层新增：开关关闭时候选行与开启前一致、快捷命令位置不被英文占用、选中英文候选提交的是该词本身。

## 已确认的行为

- **词库分层是必需的而非优化。** 先用「原有词表 + 无词频大表」两层实现时，`pallad` 补不出 `palladium`——它被 `palladia`、`palladic` 按词长挡在三个候选之外。加入真实词频层后，`palladium`（zipf 3.17）落在中频层 999583，而 `palladia`、`palladic` 不在词频表中、落到低频层 96694。该失败与修复均已固化为测试。
- **词库许可已逐一核对。** Norvig 的 33 万词表因数据无明确许可、且源自 LDC 商业语料而未采用。最终采用 dwyl/english-words（Unlicense，公有领域）与 wordfreq（数据 CC BY-SA 4.0）。CC BY-SA 强制署名 SUBTLEX 作者，已写入 `LICENSE_NOTICE.md`、`third_party/english-wordlist/PROVENANCE.md`，并随包安装到 `bin/licenses/EnglishWordlist/`。

## 开发机与他人机器的差异（重要）

本机配置与常规 Windows 有两处不同，导致两类故障在此永远测不出来，只在分发后暴露：

- **`ACP = 65001`**（开启了「使用 Unicode UTF-8 提供全球语言支持」）。Windows PowerShell 5.1 因此把无 BOM 的 UTF-8 脚本读对了，而常规简体中文 Windows 的 `ACP = 936`，同一批脚本在那里会因中文注释乱码而解析崩溃。已实测于他人机器，现已给全部含中文的 21 个 `.ps1` 加上 UTF-8 BOM，并在 `one_click_update_regression.ps1` 中改为**直接断言文件字节**——因为「跑一遍看报不报错」在本机是假通过。
- **智能应用控制关闭**。他人机器开启强制模式，安装器被直接拦下。

结论：涉及脚本编码、签名、系统防护的验证，本机结果不可采信，必须在常规配置的机器上复验。

## 尚未验证（阻止转为正式版）

- **英文候选未实机使用。** 位置阈值是否符合真实手感、双拼阈值是否需要调整、低频层是否带来噪音，都必须实际输入后才能判断。这三项都只需改常量或重跑词库构建脚本，不牵动逻辑。
- **安装器新界面未实机运行。** 确认页、进度页、完成页均未在真实安装流程中走过。
- **一键更新原地升级未实机运行。** UAC 由两次降为一次的实际效果、`-CleanReinstall` 分支均未实跑。
- **重启后文件保留未验证。** v0.7.16 修复的核心缺陷需要「升级 → 重启 → 确认文件仍在」的完整闭环才能确认。
- **Windows 搜索候选可见性**已在 v0.7.16 实机确认（Windows 11 家庭版中文版 Build 26200、125% 缩放），但未在其他缩放比例下复验。
- Host 8 小时、TSF/App 8 小时、P0 真实宿主矩阵均未运行。
- **无可信 Authenticode 签名与 RFC 3161 时间戳。** 已在他人机器上实测到后果：开启智能应用控制（`VerifiedAndReputablePolicyState=1`）的 Windows 11 直接拦下 `PiInput-Install.exe`，提示「因为无法验证其发布者」，且强制模式不提供绕过入口，只能整体关闭该功能（不可逆）。包内全部 exe 的 `Get-AuthenticodeSignature` 均为 `NotSigned`。这是分发给他人的硬阻塞，写多少代码都不能绕开。
- 词库扩大约 12 倍（378 KB → 4.39 MB），Host 常驻内存的实际增量未测量。

## 智能应用控制拦截的实测证据

2026-08-26 在他人机器（Windows 11 家庭版中文版 Build 26200 / 25H2）取得的诊断：

- `VerifiedAndReputablePolicyState = 1`，即强制模式。
- 事件日志 `Microsoft-Windows-CodeIntegrity/Operational` 记录 Id 3033 与 3077：

  > Code Integrity determined that a process (`explorer.exe`) attempted to load
  > `PiInput-Install.exe` that did not meet the Enterprise signing level requirements
  > or violated code integrity policy (Policy ID:`{0283ac0f-fff1-49ae-ada1-8a933130cad6}`)

**这是内核代码完整性层的拒绝，不是应用层提示框。** 因此不存在用户态绕过途径：命令行、PowerShell 与双击的结果相同，弹窗上的「正常」只是关闭对话框。

**推论：拦截不止于安装。** 拒绝发生在加载 PE 文件的时刻，而 PiInput 的运行方式是把 `PiInputTSF.dll` 加载进每一个接受输入的进程。同一策略会同样拒绝该 DLL，因此在强制模式的机器上，即使绕过安装步骤，输入法也无法工作。签名是唯一解，不存在"先凑合用"的中间状态。

包内全部 exe 与 dll 的 `Get-AuthenticodeSignature` 均为 `NotSigned`，v0.7.13 起各版本皆然，签名状态从未变化。

诊断脚本保留在 `scripts/dev/diagnose-smart-app-control.ps1`，只读，可直接发给遇到拦截的用户运行。

## 结论

自动回归完整通过，词库分层与许可合规已确认。但英文候选、安装器界面、升级重启闭环均未实机验证，签名门禁未完成。**只能作为候选版分发，不得转入 `releases/current`。**

对开启智能应用控制强制模式的机器，本版与此前任何版本一样无法安装或运行，且这不是可通过改代码解决的问题。
