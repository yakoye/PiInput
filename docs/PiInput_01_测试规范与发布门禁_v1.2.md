# PiInput 中文输入法测试规范与发布门禁

> Test Specification & Release Gate · v1.2  
> 更新日期：2026-08-23

## 修改记录

| 版本 | 修改内容 | 日期 |
|---|---|---|
| v1.0 | 初版 | 2026-08-23 |
| v1.1 | 增加指标、兼容矩阵和 Release Gate | 2026-08-23 |
| v1.2 | 结合当前源码、CTest、CI/签名/包闭环、8h soak 和智能标点现状重写；区分能力、脚本、执行证据和真人验收 | 2026-08-23 |

## 1. 文档定位

本文定义“一个 PiInput 构建满足什么条件才可以进入 RC 或正式发布”。它不以计划文档、脚本文件存在或开发者主观体验作为完成证据。

详细用例见 [PiInput_02_测试用例集_v1.1.md](PiInput_02_测试用例集_v1.1.md)，自动化架构见 [PiInput_03_PiInputTest自动化测试程序设计_v1.1.md](PiInput_03_PiInputTest自动化测试程序设计_v1.1.md)，标点语义见 [PiInput_05_智能符号与中英文标点语义规则_v1.1.md](PiInput_05_智能符号与中英文标点语义规则_v1.1.md)。

## 2. 证据等级

| 等级 | 证据 | 可支持的结论 |
|---|---|---|
| E0 | 设计、计划、脚本尚未执行 | 只能说明目标或测试入口存在 |
| E1 | 源码检查、静态门禁 | 说明结构/策略存在，不能证明运行时行为 |
| E2 | 单元、组件、进程内/进程间自动化 | 说明受控输入下行为可复现 |
| E3 | 打包后安装/升级/卸载闭环 | 说明交付物而非开发目录可工作 |
| E4 | 真实宿主人工或 UI 自动化验收 | 说明具体应用和控件行为通过 |
| E5 | 长时运行、正式签名、公开产物复核 | 支持正式发布结论 |

发布报告必须给出实际证据路径、时间和构建身份。`NOT RUN`、`BLOCKED` 和 `PASS` 必须严格区分。

## 3. 当前实现基线（2026-08-23）

| 领域 | 当前状态 | 已有证据 | 仍缺少 |
|---|---|---|---|
| 核心引擎/Host/TSF | 较完整 | 当前 Release 构建和 65/65 CTest 已通过；新增 Controlled TSF Host 与 UAC manifest 自检均纳入全量回归 | 真实宿主矩阵和下一冻结候选长时验证；不得用组件测试替代 |
| 敏感输入 | 已实现，待扩大真人验收 | `GUID_PROP_INPUTSCOPE`/`ITfInputScope` 策略测试；Controlled TSF Host 已显式发布 Password/Numeric PIN scope，真实配置 smoke 已包含旁路 Oracle | 当前候选 DLL 的受控 smoke 实跑；浏览器、WinUI、凭据框实测矩阵 |
| 结构化语料 | 已实现 | 313 条结构化语料，其中专业词 59 条 | 固定版本产物的最终报告归档 |
| 大词库内存映射 | 已实现，8h 运行中 | `.lex` 只读映射；Host health 报告 `lexicon_storage=mmap` 和映射字节；短时稳态回归通过 | 完整 8h/24h 曲线与大词库多宿主运行 |
| 智能标点 | 核心交付子集完成，完整规范未完 | 纯规则引擎、临时 composition、Scintilla 文档真值和 reason code 已实现；严格 token、技术符号、千位三位 provisional 及 Backspace/Esc/context 销毁 Oracle 已加入；新 DLL 在 Notepad++ 通过基础矩阵 | 当前候选 DLL 的 Controlled TSF smoke/生命周期实跑；更多边界；ChatGPT/Chromium/Office/VS Code 同构建矩阵 |
| Windows CI | 脚本与 workflow 已存在 | Windows build/CTest、JUnit、固定词库、tag/VERSION/clean 检查、可选签名、失败证据上传、包闭环阶段化失败 summary、统一 `result.json`/artifact manifest 和公开资产回下载哈希步骤已写 | GitHub 在线执行记录及失败恢复验证 |
| 代码签名 | 工具链已写，证据缺失 | `sign-binaries.ps1` 使用 SHA-256/RFC3161；逐 PE 验证签名和时间戳证书，并记录签名者/时间戳指纹与文件哈希；PFX/密码仅暴露给单个签名步骤并在 `finally` 删除临时文件 | 正式证书、tag 构建签名结果、证书链复核 |
| 安装包闭环 | 普通用户架构完成，真实闭环待跑 | 安装/卸载 manifest 已改为 `asInvoker`；安装器直接注册并轮询当前用户 profile，不再执行重复 `--refresh-profile`；卸载器通过控制管道停止 Host 并直接调用 TSF API，不再提升权限、启动 Host/profile 子进程或加载产品 DLL；所有交互框置顶 | 用受信任签名候选在标准用户和启用应用控制的干净机器执行安装×2、卸载和重装闭环 |
| 8h soak | 冻结候选 Host-only 8h 已通过；TSF/App harness 已实现 | `a2d5f8fe3c53` / `0.7.13+a2d5f8fe3c53` 共 957 样本，Private/WS/Handle 增量和斜率均通过且 mmap=40,758,365 bytes；TSF/App 控制器可循环标点、敏感 scope 和 context 重建 | 当前候选安装后的 TSF/App smoke 与 8h |

当前仓库不能因为“CI/签名/soak 脚本已经存在”就宣布对应发布门禁通过。

## 4. 要求分级

| 级别 | 含义 | 发布处理 |
|---|---|---|
| MS-REQ | Microsoft/Windows 平台要求 | 正式版必须满足并提供权威来源或工具证据 |
| PI-GATE | PiInput 内部硬门禁 | 不通过则禁止发布 |
| PI-TARGET | 体验目标 | 未达标须记录基线、影响和批准的豁免 |
| BENCH | 同机同语料对标 | 用于方向判断，不直接替代正确性门禁 |

## 5. 发布优先级

```text
隐私与系统安全
  > 输入完整性
  > TSF/context 状态机正确性
  > 真实宿主兼容性
  > 稳定性和资源曲线
  > 安装/签名/产物闭环
  > 延迟与中文质量
  > UI 细节
```

候选排序仍可迭代；丢键、重键、跨输入框回写、密码框泄漏、宿主崩溃、标点规则破坏基本输入均为阻断项。

## 6. Gate 定义

### G0 构建身份与可追溯性

- 源码 commit、dirty 状态、版本、build ID 和构建配置可追溯。
- `PiInputHost.exe --version` 与 `VERSION` 一致。
- build ID 包含版本和源码身份；dirty 构建不得作为正式发布。
- ZIP、安装后文件和 Release 资产可通过 SHA-256 对应。

### G1 静态与自动化回归

- Release 构建成功。
- 当前 CMake 注册测试全部执行，不能只报告“已注册 63 项”。
- `Failed=0`；skip/disabled 必须逐项说明原因。
- 结构化语料、专业词、字典脚本、source regression、Host process、性能 smoke 结果归档。

### G2 输入完整性

| 指标 | 正式版门槛 |
|---|---|
| Lost Key | 0 |
| Duplicate Commit | 0 |
| Unexpected Commit | 0 |
| Wrong Commit | 0 |
| Cross-context Write | 0 |
| Composition/Candidate Stuck | 0 |
| Host/App Crash or Hang caused by PiInput | 0 |

计数为 0 必须同时给出事件数、场景、宿主和观测方式，不能只写零。

### G3 隐私与输入范围

- Password、PIN、numeric password、private scope 完全旁路 Host、候选、composition 和学习。
- 输入框在普通/敏感 scope 间复用同一 TSF context 时仍能即时切换策略。
- 日志不包含敏感文本；诊断开关不得绕过此限制。
- 至少完成浏览器密码框、Win32 密码框、WinUI/Windows App 输入框和凭据类场景验收。

### G4 智能标点

- `SP-MIX-*`、`SP-STATE-*`、`SP-PRIV-*` 用例通过。
- `1. 第一项`、`12:23`、`2/3`、`v1.0.1。`、`白色/黑色，白色、黑色。` 可在中文模式连续输入。
- 规则不依赖直通键之后的 `OnKeyDown`。
- 快速输入中标点和下一数字顺序不乱。
- Notepad++ 与 ChatGPT 的结果一致。

当前核心子集可以按组件和 Notepad++ 分项记录 `PASS`；在 05 的 P0 规则族、状态用例和同一候选构建跨宿主矩阵全部完成前，整个 G4 仍为 `BLOCKED`。

### G5 真实宿主兼容性

| 类别 | P0 宿主/场景 | 证据要求 |
|---|---|---|
| Win32/Scintilla | Notepad、Notepad++ | 基础输入、数字标点、焦点、粘贴后输入 |
| Chromium/Web | Chrome、Edge | 普通/密码输入框、tab/context 复用 |
| Electron | VS Code、ChatGPT Windows App | 连续输入、失焦、粘贴、快速标点 |
| Office | Word、Excel | composition、单元格/段落切换 |
| Windows App/WinUI | 设置、搜索等 | profile、候选位置、敏感范围 |
| Terminal | Windows Terminal、PowerShell/CMD | 命令符号、路径、中文提交 |
| 聊天/IM | 微信、企业微信/Teams 等 | 多输入框、粘贴、焦点与候选生命周期 |
| 生命周期 | 锁屏、睡眠、IME 切换 | 恢复后直接可输入 |

“某个旧进程仍加载旧 DLL 且好用”不能证明新安装包在该宿主通过；必须记录 PID、加载模块身份或重启边界。

候选窗口还必须覆盖 100/125/150/200% DPI、混合 DPI 多屏、屏幕边缘避让、系统输入指示器和可访问性；不能只验证最终文本。

### G6 性能与资源

#### G6.1 性能

性能分成两层：

1. 引擎/Host 受控性能：现有 benchmark 和 Host process 测试自动执行。
2. UI 端到端性能：从物理/注入按键到宿主可见文本或候选更新，必须在真实或受控 TSF 宿主测量。

| 指标 | 目标 | 备注 |
|---|---|---|
| Key -> Composition P95 | <16 ms | PI-TARGET，需端到端观测 |
| Key -> Candidate P95 | <30 ms | PI-TARGET |
| Commit P95 | <20 ms | PI-TARGET |
| 热 Host 请求 P95 | 以冻结基线防回退 | 不能与 UI 端到端指标混用 |
| 冷启动首次可用 | 建立分位数基线后定 Gate | 当前单次测量不能代替分布 |

候选翻页和中英文/IME 模式切换也必须输出 P50/P90/P95/P99/MAX；初稿目标分别为 P95 <30 ms 和 P95 <50 ms，在端到端观测器落地前属于 `PI-TARGET`，不能伪填为 PASS。

#### G6.2 稳定性与资源

资源门禁：

- 8h 必测，24h 建议。
- 同时记录 Private Bytes、Working Set、Handle、Thread、CPU；GUI 宿主还记录 GDI/USER。
- 同时判断起止增长、峰值和线性斜率。
- 短时 smoke 只能验证 harness，不得替代 8h 结论。
- Host-only soak 不能替代真实 TSF 宿主 soak；两者报告必须分开。
- TSF/App soak 默认每小时至少完成 100 个 workload iteration，并按每 20 轮至少一次的策略核对 `context_recreates`；“进程空转 8 小时”不得 PASS。
- TSF/App soak 必须同时门禁应用进程和已注册 PiInput Host 的 Private/Working Set/Handle 增量与斜率；只看其中一个进程不得 PASS。
- Host 资源采样身份由默认 health pipe 返回的 `host_pid` 与注册 `CurrentHostPath` 双重确认；不得在同路径多进程中随意取第一个 PID。

| 压力场景 | RC 最小规模 | 硬判定 |
|---|---|---|
| 按键压力 | 100k key events；长期提升到 1M | Lost/Duplicate/Wrong/Unexpected/Crash=0 |
| 焦点切换 | 10k | Stuck composition/candidate=0，资源回稳态 |
| IME 切换 | 10k | 切换失败/错模式/Crash=0 |
| Context 创建/销毁 | 10k | Crash=0，Handle/Thread/GDI/USER 不持续爬升 |
| 睡眠/唤醒、锁屏/解锁 | 建议各 100 次 | 恢复后直接可输入，状态一致 |

#### G6.3 中文输入质量

| 指标 | 口径 | Gate 处理 |
|---|---|---|
| Top1 Accuracy | 第一候选即目标 | 冻结词库、设置和分类语料，按版本比较 |
| Top5 Recall | 目标进入前五 | 防止词库/排序回退 |
| 平均选词次数 | 完成目标文本的额外选择/翻页数 | 越低越好，按类别报告 |
| 长句一次上屏率 | 整句首选直接提交 | 短句、长句、技术词分栏 |
| 个性化收益 | 学习前后命中变化 | 必须同时验证错误学习可撤销/衰减 |

当前 313 条结构化语料和 59 条专业词只证明已登记样本的确定性回归；它不是原初建议规模（单字 1k、常用词 5k、短句 5k、长句 2k、技术词 2k、网络词 1k、英中混输 1k），也不能替代与主流输入法的同机同语料 BENCH。正式 Gate 先冻结 PiInput 自身基线，竞品对标作为趋势证据，不拍脑袋设单一准确率。

### G7 签名与包闭环

- 正式 tag 必须有受信任的 Authenticode 代码签名证书，测试证书无效。
- 所有包内 EXE/DLL 使用 SHA-256 摘要和 RFC3161 时间戳并验证证书链。
- 包闭环至少验证：ZIP 哈希、唯一包根、必需文件、禁止源码/脚本泄漏、版本/build ID、签名和时间戳状态、静默安装、覆盖安装、静默卸载。
- 安装后必须比较包内与实际运行 `PiInputHost.exe`/注册 `PiInputTSF.dll` 的 SHA-256，核对 `CurrentHostPath` 与 CLSID 注册路径，并由受控 TSF 宿主验证实际加载模块路径；只看卸载项版本号不算闭环。
- 必须在干净用户配置执行一次；在已有安装上执行一次升级路径。
- 安装后实际 DLL/EXE 哈希与包内文件对应。
- 从前一正式版升级和 RC -> Final 必须验证无双 profile/旧 DLL；用户词库、配置、主题和双拼方案按产品策略保留或迁移。
- 卸载后不得残留失效 TSF profile、启动项、无效语言项、孤儿运行时或缓存。

没有正式证书时，允许生成开发候选，但 Release Gate 必须是 `BLOCKED`，不能通过关闭 `RequireSigned` 变成正式版。

### G8 发布资产闭环

- 正式 tag 在构建和签名前先读取对应 `VERIFICATION_v<version>.md` 的机器 Gate；`host_soak_8h`、`tsf_app_soak_8h`、`p0_real_host_matrix` 任一不是 `PASS` 时 fail-closed。
- CI 的 `always()` 收尾必须用 `compose-test-result.ps1` 汇总构建、外部 Gate、签名和包闭环；Case ID 重复、状态非法或声明的 artifact 缺失时失败，`BLOCKED/NOT_RUN/N/A` 不得计入执行通过率。
- tag、源码 commit、release notes、ZIP 名称、内部版本、build ID 和 SHA-256 一致；CI 按 `VERSION + git rev-parse --short=12 HEAD` 生成期望 build ID，包内与安装后 Host 必须精确相等，不能只比较版本前缀。
- CI 产物通过后再发布，禁止拿本地未验证文件替换。
- 公开下载资产需要再次下载并校验哈希。
- Release 报告和已知问题随资产归档。

## 7. 回归层级

| 集合 | 触发时机 | 要求 |
|---|---|---|
| Developer Smoke | 每次相关修改 | 相关单元/组件测试 + source regression |
| PR Gate | 每次合入 | Release build + 全 CTest + 短 Host process/语料/性能 |
| Nightly | 每日/按需 | 大词库、受控压力、性能趋势、短 soak/fuzz |
| RC | 候选版 | 全 P0/P1、真实宿主矩阵、8h、升级/卸载、候选签名验证 |
| Final | 正式发布 | 干净构建、正式签名、关键 Gate 重跑、公开资产复核 |

## 8. 缺陷等级

| 等级 | 定义 | 示例 | 发布规则 |
|---|---|---|---|
| P0 Blocker | 基础输入、隐私或系统级严重问题 | 无法输入、跨密码框泄漏、宿主崩溃、大规模丢键 | 必须为 0 |
| P1 Critical | 核心路径可复现错误 | 重复提交、标点语义破坏高频输入、候选卡死、持续泄漏 | 正式版必须为 0 |
| P2 Major | 有替代路径但明显影响体验 | 某类控件错位、特定规则缺失、长尾卡顿 | 需书面评审 |
| P3 Minor | 低频视觉或文案问题 | 轻微间距、非关键文案 | 可作为已知问题 |

## 9. 发布结论规则

| 结论 | 条件 |
|---|---|
| PASS | 所有硬 Gate 通过，P0/P1=0，证据完整 |
| CONDITIONAL PASS | 仅存在经批准的 PI-TARGET/P2 豁免；不得用于绕过签名、隐私、输入完整性和 P0/P1 |
| BLOCKED | 需要证书、8h 时间窗、真实宿主或外部环境，尚未执行 |
| FAIL | 已执行且结果不符合门槛 |

`BLOCKED` 不是 `FAIL`，也不是 `PASS`。报告必须保留这一区分。

## 10. 当前候选的门禁结论

基于 2026-08-23 工作区状态：

- G0/G1：当前标准 Release 构建和 65/65 CTest 已通过。
- G2/G3：受控自动化基础较好，但仍缺真实宿主完整矩阵。
- G4：智能标点核心实现和 Notepad++ 新 DLL 矩阵已通过；其余 P0 宿主矩阵仍为 `BLOCKED`。
- G5：Notepad++ 新版本根因与实机闭环已完成；ChatGPT/Chromium 等同构建验证仍为 `BLOCKED`。
- G6：冻结候选 `a2d5f8fe3c53` 的 Host-only 8h 已通过；TSF/App harness 与 fixture smoke 已建立，但当前候选真实 smoke/8h 尚未执行，因此 G6 整体仍为 `BLOCKED`。
- G7：普通用户安装/卸载架构与静态回归已通过；签名/时间戳清单、安装后路径/哈希/实际 DLL 身份和公开资产回下载门禁已写，无正式证书且干净机实跑证据缺失，仍为 `BLOCKED`。
- G8：只有在前述 Gate 通过后才能执行。

因此当前工作区不是正式发布候选。

## 11. 参考资料

- Microsoft Learn: [Input Method Editor (IME) requirements](https://learn.microsoft.com/windows/apps/develop/input/input-method-editor-requirements)
- Microsoft Learn: [Input Method Editors (IME)](https://learn.microsoft.com/windows/apps/develop/input/input-method-editors)
- Microsoft Learn: [Text Services Framework](https://learn.microsoft.com/windows/win32/tsf/text-services-framework)
