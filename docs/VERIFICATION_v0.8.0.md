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

## 尚未验证（阻止转为正式版）

- **英文候选未实机使用。** 位置阈值是否符合真实手感、双拼阈值是否需要调整、低频层是否带来噪音，都必须实际输入后才能判断。这三项都只需改常量或重跑词库构建脚本，不牵动逻辑。
- **安装器新界面未实机运行。** 确认页、进度页、完成页均未在真实安装流程中走过。
- **一键更新原地升级未实机运行。** UAC 由两次降为一次的实际效果、`-CleanReinstall` 分支均未实跑。
- **重启后文件保留未验证。** v0.7.16 修复的核心缺陷需要「升级 → 重启 → 确认文件仍在」的完整闭环才能确认。
- **Windows 搜索候选可见性**已在 v0.7.16 实机确认（Windows 11 家庭版中文版 Build 26200、125% 缩放），但未在其他缩放比例下复验。
- Host 8 小时、TSF/App 8 小时、P0 真实宿主矩阵均未运行。
- 无可信 Authenticode 签名与 RFC 3161 时间戳。
- 词库扩大约 12 倍（378 KB → 4.39 MB），Host 常驻内存的实际增量未测量。

## 结论

自动回归完整通过，词库分层与许可合规已确认。但英文候选、安装器界面、升级重启闭环均未实机验证，签名门禁未完成。**只能作为候选版分发，不得转入 `releases/current`。**
