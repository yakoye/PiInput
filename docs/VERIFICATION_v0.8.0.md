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

**这是内核代码完整性层的拒绝，不是应用层提示框。** 弹窗上的「正常」只是关闭对话框，不构成放行。

**但拦截的范围远比"未签名即拒绝"要窄，实测如下：**

| 文件 | 名称像安装器 | 依赖 comctl32 | 该机器首次见到 | 结果 |
| --- | --- | --- | --- | --- |
| v0.7.14 `PiInput-Install.exe` | 是 | 否 | 否 | 正常运行 |
| v0.7.14 `PiInput-Uninstall.exe` | 否 | **是** | 否 | 正常运行 |
| v0.7.16 `piinput-diagnostics.exe` | 否 | 否 | **是** | 正常运行 |
| v0.7.16 `PiInput-Install.exe` | **是** | 是 | **是** | **被拒绝** |
| 已安装的 `PiInputTSF.dll` | 否 | 否 | 否 | 正常加载，输入法工作中 |

以上全部未签名。comctl32 与 Common Controls 6 清单单独出现（卸载器）不触发拦截，全新文件单独出现（诊断程序）也不触发；**只有"名称像安装器"与"该机器未见过此文件"同时成立时才被拒绝**。这与安全软件对安装程序单独抬高判定门槛的通行做法一致。因此可排除 v0.7.16 为安装器新增的 comctl32 依赖。

同一台强制模式的机器上，同为未签名，只有一个文件被拒。因此**不能得出"未签名二进制一律无法运行"的结论**：微软文档中"app intelligence services provide safety predictions"这条路径确实在起作用，签名只是绕过判定的其中一种方式。此前本文档曾据单点观察推广出该结论，属推断过度，已更正。

**已排除文件名因素。** 将同一份 `PiInput-Install.exe` 复制并改名为 `pi-deploy-test.exe` 后运行，仍被拒绝。

**定位结论：安装程序行为特征 + 该文件无判定记录。** 同为 v0.7.16 的全新文件，控制台程序 `piinput-diagnostics.exe` 可正常运行，而安装器被拒。两者的区别在于安装器请求提权、写入系统目录、注册 COM——这组特征正是安装程序的画像，也是恶意软件最常见的形态，判定门槛因此更高。v0.7.14 的安装器具备同样特征却能运行，只能是它已被判定服务记录（此前多次运行过）。

**后果：该"资格"不随版本继承。** 每个新版本的安装器都是新哈希、无记录，因此都会在启用强制模式的机器上被拒绝一次，直到取得签名或积累判定。

**签名的技术要求：必须使用 RSA。** 微软文档明确 Smart App Control 的签名校验目前不支持 ECC（椭圆曲线）证书，选购时须确认算法，否则签名无效。

包内全部 exe 与 dll 的 `Get-AuthenticodeSignature` 均为 `NotSigned`，v0.7.13 起各版本皆然，签名状态从未变化——这也说明签名并非 v0.7.14 与 v0.7.16 行为差异的原因。

诊断脚本保留在 `scripts/dev/diagnose-smart-app-control.ps1`，只读，可直接发给遇到拦截的用户运行。

## 结论

自动回归完整通过，词库分层与许可合规已确认。但英文候选、安装器界面、升级重启闭环均未实机验证，签名门禁未完成。**只能作为候选版分发，不得转入 `releases/current`。**

对开启智能应用控制强制模式的机器，本版与此前任何版本一样无法安装或运行，且这不是可通过改代码解决的问题。
