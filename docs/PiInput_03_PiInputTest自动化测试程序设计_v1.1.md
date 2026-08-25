# PiInputTest 自动化测试程序设计

> Automation Harness Design · v1.1  
> 更新日期：2026-08-23

## 修改记录

| 版本 | 修改内容 | 日期 |
|---|---|---|
| v1.0 | 初版，提出独立自动化框架 | 2026-08-23 |
| v1.1 | 改为增量建设：复用现有 CTest、Host fixture、PowerShell 回归、诊断和发布脚本；补充 Smart Punctuation、包闭环、资源曲线与真实宿主分层 | 2026-08-23 |

## 1. 设计结论

PiInputTest 不应先另起一个与仓库现有测试平行的大工程。当前已经有 65 个 CTest、原生 C++ fixture、Host 进程测试、PowerShell 回归、结构化语料、benchmark、Windows CI、包闭环和 soak 脚本；新增项包含 Controlled TSF Host 与安装/卸载 UAC manifest 自检。

当前已完成受控 Win32 TSF Host、物理扫描码注入、焦点/context 控制、最终文本 Oracle，以及可选的真实 PiInput profile/DLL 身份模式。下一阶段继续把这些执行器纳入统一“场景与证据层”，补齐仍缺失的能力：

1. 把 Smart Punctuation 临时状态、reason code 和已实现的 Controller Oracle 接入统一场景报告；
2. 扩展 IME 切换、候选选择、Backspace/Esc 和快速顺序场景；
3. 统一 JSON/JUnit/CSV/HTML 报告；
4. 在已有 Host-only soak 之外补真实 TSF/App 8h soak；
5. 扩展到 WinUI/WPF 和真实应用适配器。

## 2. 当前资产清单

| 资产 | 当前能力 | 不应误认为 |
|---|---|---|
| CTest 65 项 | 当前 Release 全量 65/65 已通过，包含受控宿主和 UAC manifest 自检 | 真实应用矩阵已自动化或当前冻结候选再次全量 65/65 PASS |
| `piinput-host-client-fixture` | 驱动真实 Host 协议与负载 | 驱动了 TSF/Notepad++ |
| `host_process_tests.ps1` | Host health、协议、身份、性能 smoke | 端到端按键到 UI 延迟 |
| `structured_corpus_regression.ps1` | 313 条结构化语料和 59 条专业词 | 用户真实键盘/候选 UI 验收 |
| `piinput-key-trace-<PID>.csv` | 隔离每个 Shim 进程的时序 | 完整语义 reason code |
| `host_soak_tests.ps1` | Host workload、mmap、资源采样和斜率；支持并发读取的追加采样 | 已经运行满 8h；真实 TSF soak |
| `windows-release.yml` | Windows build/CTest、tag/clean 身份、签名、失败证据、包闭环、Release 回下载 | workflow 已在线成功运行且公开资产哈希与本地产物一致 |
| `verify-package-closure.ps1` | ZIP/身份/签名与时间戳/前版升级与 UserData 哨兵/安装×2/HKCU+HKLM COM 与 Host 路径及哈希/受控实际 DLL/卸载 | 正式证书和干净机已经通过 |

## 3. 自动化分层

| 层级 | 目的 | 执行器 | 当前状态 |
|---|---|---|---|
| L0 Pure Unit | parser、词典、排序、Smart Punctuation 纯规则 | C++ tests | 已有，含严格小数/版本/IPv4/时间/比例/千位、URL/Email/Path/File、技术中缀/边界、中文反例和 pending 解析 |
| L1 Component | HostSession、protocol、mirror、InputScope、TSF 状态模型 | C++ tests/替身 | 已有基础；千位三位 provisional、回退/保数字策略和异步完成已有代码与源码门禁 |
| L2 Process | 真 Host/pipe/fixture/installer logic | CTest + PowerShell | 较完整 |
| L3 Controlled TSF Host | Win32/WPF/WinUI 等可观测文本控件 | `piinput-controlled-tsf-host` + `piinput-controlled-tsf-controller` | Win32 fixture、物理扫描码、焦点/context Oracle 已通过；真实 PiInput profile、DLL 身份、标点、Backspace/Esc、跨 context/销毁及 Password/PIN smoke 已实现，待当前候选安装后执行 |
| L4 Real App | Notepad++、浏览器、VS Code、ChatGPT、Office | UIA + 人工核验 | 以人工为主，自动化待建 |
| L5 System/Release | 锁屏、睡眠、DPI、签名、安装包、8h/24h | PowerShell/CI/人工 | 脚本部分已有，证据待完成 |

## 4. 总体架构

```text
Scenario Manifest
      |
      v
PiInputTest Controller
  |-- CTest Adapter
  |-- Process Fixture Adapter
  |-- Input Injector
  |-- Focus / Window / IME Controller
  |-- Controlled TSF Host Adapter
  |-- Real App Adapter
  |-- Oracle / Verifier
  |-- Metrics / Resource Sampler
  |-- Watchdog / Dump Collector
  `-- Report Merger
          |
          +-- result.json
          +-- junit.xml
          +-- metrics.csv
          +-- evidence manifest
```

Controller 只负责组织场景、关联证据和输出结论，不复制 CTest 已有的业务逻辑。

## 5. 场景格式

推荐使用 JSON，避免在第一阶段增加 YAML 解析依赖：

```json
{
  "schema_version": 1,
  "case_id": "SP-MIX-003",
  "priority": "P0",
  "layer": "L3",
  "host": "win32_test_host",
  "preconditions": {
    "input_mode": "chinese",
    "punctuation_mode": "chinese"
  },
  "steps": [
    {"focus": "edit_a"},
    {"keys": ["MEETING_TEXT", "SHIFT+OEM_1", "1", "2", "SHIFT+OEM_1", "2", "3", "OEM_PERIOD"]}
  ],
  "assert": {
    "final_text": "会议时间：12:23。",
    "lost_key": 0,
    "duplicate_commit": 0,
    "stuck_composition": false
  },
  "timeout_ms": 5000
}
```

场景必须记录物理按键，不能只使用 Unicode 文本注入；后者会绕过 IME 按键语义。

## 6. 统一关联模型

每个事件至少携带：

```text
run_id
case_id
scenario_step
event_seq
process_id
thread_id
context_id_hash
session_id
request_sequence
generation
build_id
monotonic_timestamp
```

`context_id` 必须是运行期不可逆标识，不能把窗口标题、用户文本或敏感字段写入日志。

## 7. 事件模型

| Event | 最小字段 |
|---|---|
| `KEY_SENT` | seq、vk/scancode、down/up、modifier、t_send |
| `KEY_TESTED` | eaten、scope_class、decision_cache_id |
| `KEY_RECEIVED` | seq、context、t_receive |
| `PUNCT_DECISION` | source_key、token types、rule_id、action、provisional state |
| `COMPOSITION_START/UPDATE/END` | context、generation、length、caret；非敏感诊断才允许限长文本 |
| `CANDIDATE_UPDATE` | count、selected、generation、t |
| `COMMIT` | length/hash、generation、t；测试宿主可单独保存 expected/actual |
| `FOCUS_CHANGE` | old/new context、reason |
| `IME_SWITCH` | from/to、mode |
| `CONTEXT_CREATE/DESTROY` | context、host process |
| `HOST_TEXT_SNAPSHOT` | controlled host 的完整文本或 hash |
| `RESOURCE_SAMPLE` | CPU、private、working set、virtual、handle、thread、GDI/USER |
| `PACKAGE_IDENTITY` | version、build ID、hash、signature status |
| `CRASH/HANG` | process、exception/timeout、dump/evidence |

## 8. Smart Punctuation 自动化设计

### 8.1 Pure Engine

为 `SmartPunctuationEngine` 建立表驱动 L0 测试：

```text
PunctuationContext -> PunctuationDecision
```

规范目标覆盖：确定性数字、序号、技术 token、URL/path、中文正文、已有右侧文本和歧义进入临时状态。当前表驱动实现已覆盖 `/` 字面输入、严格小数/版本/IPv4/时间/比例/千位、URL/Email/Path/File、技术中缀/边界/前缀/引号、方括号/圆括号/下划线、行首数字序号、普通中文标点及数字后 provisional 解析；Controlled TSF lifecycle Oracle 已写，当前候选实跑和跨真实宿主仍不能写成已覆盖。

### 8.2 TSF Adapter

L1/L3 需要验证：

- 同步只读 edit session 获取选择左右上下文；
- `OnTestKeyDown` 无写操作；
- 未吃掉直通键之后没有 `OnKeyDown` 时仍然正确；
- Resume/focus 只属于会话同步，不得计入 composition；只有镜像中存在 raw，或真正的按键请求尚未返回，才允许数字进入候选选择路径；
- 新 context 的 Resume 仍在途时连续输入 `123`，三个数字必须全部透传；快速输入拼音后紧接数字时，数字仍须在对应按键请求之后有序选词；
- 临时标点 composition 仅拥有一个符号；
- 下一数字与标点原子有序；
- Backspace/Esc/focus/context destroy 清理正确；
- 不修改不属于 PiInput 的已提交 range。

### 8.3 真实宿主

Notepad++ 是 Smart Punctuation 的 P0 首要宿主，因为它已经暴露出“直通键只有 `OnTestKeyDown`”的差异。ChatGPT Windows App 作为另一种宿主模型同时验证，不能用其中一个替代另一个。

Windows 搜索必须单列为打包系统宿主：除了 profile/category、`IMMERSIVESUPPORT` 和 DLL 的 `ALL APPLICATION PACKAGES` 读取执行权限，还要核对 64 位 HKLM COM 入口。HKCU/HKLM 都必须指向 Program Files 下仅管理员可写的同一 Shim；绝不能让机器 COM 指向用户可写的 LocalAppData。只存在 HKCU `InprocServer32` 时，普通桌面应用可以加载，而 SearchHost 看不到该类；测试必须记录 SearchHost PID、实际模块路径和中文最终文本。候选 UI 另有独立断言：Shim 必须在 edit session 内取得 `ITfContextView::GetWnd`，失败时回退当前焦点，转换为顶层 HWND 后随 caret 消息传给 Host；Controller 枚举可见 `PiInputTsfCandidateWindow` 并断言 `GetWindow(candidate, GW_OWNER)` 等于测试宿主。还要销毁宿主后再输入，验证 Host 不复用失效 HWND。

## 9. Controlled TSF TestHost

第一版只做 Win32 原生测试宿主，包含：

- 两个普通编辑框；
- 一个 password/PIN 输入框；
- 可创建/销毁/复用 context；
- 可导出文本、selection、focus、composition lifecycle；
- 支持窗口销毁、tab 切换和 DPI 移动；
- 提供只读 IPC/JSON 状态端点给 Controller。

后续再增加 WPF、WinUI 和 Qt，不要在 MVP 同时维护四套不成熟宿主。

## 10. Input Injector

- 使用 `SendInput` 发送 scan code/virtual key，按下和抬起都记录。
- 支持物理 OEM 键区分：`;/:`、`./>`、`//?`、`\\/|`。
- 记录键盘布局和 modifier 快照。
- Unicode 注入仅用于验证旁路文本，不用于 IME 语义用例。
- 每个动作分配 `event_seq`；失败或部分发送必须立刻终止用例。
- 不使用剪贴板模拟普通打字。

## 11. Oracle 与失败分类

Oracle 同时比较：

1. 发送的按键事件；
2. PiInput/TSF 决策与 lifecycle；
3. Host commit；
4. 受控宿主最终文本；
5. composition/candidate 最终是否回到 Idle。

| failure_class | 判定 |
|---|---|
| `KEY_NOT_RECEIVED` | 已发送但 PiInput/宿主未观察到 |
| `KEY_ORDER` | 输出顺序与事件序列不一致 |
| `DUPLICATE_COMMIT` | 同一确认产生重复文本 |
| `WRONG_COMMIT` | 提交与 expected/选中候选不一致 |
| `UNEXPECTED_COMMIT` | 未触发确认却提交 |
| `CROSS_CONTEXT_WRITE` | 写入非当前 context |
| `STUCK_COMPOSITION` | 场景结束后仍有 composition |
| `STUCK_CANDIDATE` | 候选 UI/状态未结束 |
| `PUNCT_RULE` | rule_id 或输出与规范不一致 |
| `PRIVACY_BYPASS` | 敏感范围进入 Host/日志/学习 |
| `RESOURCE_TREND` | 增长量或斜率超门槛 |
| `PACKAGE_IDENTITY` | 版本/build ID/hash/签名不一致 |

## 12. 延迟测量

统一使用 QueryPerformanceCounter 或等效单调时钟。

| Latency | 起点 | 终点 |
|---|---|---|
| L1 Key Receive | `KEY_SENT` | TSF `KEY_RECEIVED` |
| L2 Decision | context snapshot begin | decision returned |
| L3 Host | pipe send begin | reply handled |
| L4 Edit | reply handled | TSF edit completed |
| L5 Visible | `KEY_SENT` | controlled host text/candidate observed |
| Parse | parser begin | parser end |
| Dictionary | lookup begin | lookup end |
| Ranking | rank begin | rank end |
| Candidate Page | page action | candidate UI update observed |

每项输出 count、P50/P90/P95/P99/MAX 和最慢 Top N 事件链。Host-only 延迟和 UI-visible 延迟必须分栏。

## 13. Soak 与资源曲线

### 13.1 Host-only 8h

复用 `tests/host_soak_tests.ps1`：

- 强制验证 `lexicon_storage=mmap`；
- 循环中文、英文和 transport workload；
- 采样 Private/Working Set/Virtual/Handle/Thread/CPU；
- 判断起止增长与每小时斜率；
- 输出 `host-resource-samples.csv` 和 `summary.json`。

脚本必须显式加入 Nightly/RC 调度；仅存在于仓库不算执行。

### 13.2 TSF/App 8h

已新增 `tests/tsf_app_soak_tests.ps1` 与控制器持续模式：同一受控宿主循环 Smart Punctuation、Password scope 和 context 销毁/创建，原子发布 app PID、迭代数和 `context_recreates`；CSV/summary 同步保存生命周期事件量，采样 Shim 所在宿主的 Private/Working Set/Virtual/Handle/Thread/CPU/GDI/USER，并记录当前 PiInput Host 的 Private/Working Set/Handle/Thread/CPU。Host PID 由默认 health pipe 返回的 `host_pid` 与注册可执行路径双重确认，不在同路径多进程中猜测。真实模式同时门禁应用与 Host 的增长量和斜率；默认要求每小时至少 100 轮，并核对每 20 轮一次的 context 重建数量，避免空转假 PASS。采样器已用不加载 PiInput 的 fixture 模式完成正向密度 smoke，并用极高阈值验证低密度会 fail；这些只证明 harness，不证明 TSF，真实短时入口仅在显式启用交互测试且提供已注册 DLL 路径时注册，完整 8h 仍必须单独执行和归档。

两种 soak 各自独立 PASS，不能互相替代。

## 14. Fuzz 与系统生命周期

随机测试必须是可重放的约束式随机，而不是无意义键盘噪声：

- 输入事件池：字母、数字、标点、Backspace、Space、Enter、Esc、方向键、PageUp/Down、Shift/Caps 和 Ctrl 快捷键；
- 状态事件池：Focus change、Alt+Tab、IME switch、窗口/标签页创建销毁、DPI/屏幕迁移；
- 高冲突约束：候选出现后 0–30 ms 切焦点、composition 中销毁窗口、临时标点未决时切 context、快速中英文抖动；
- 每次 run 保存 seed、完整事件序列和构建身份，失败必须可 100% 重放；
- 锁屏/解锁和睡眠/唤醒不能在普通 PR 任务中随意触发，只能由隔离的系统测试节点执行，并明确恢复和超时策略。

## 15. CI 与发布流水线

| 阶段 | 自动任务 | 保留人工/外部任务 |
|---|---|---|
| PR | Release build、全 CTest、语料、Host process、性能 smoke | 无 |
| Nightly | 大词库、fuzz、短/长 soak、趋势比较 | 必要时复核失败 |
| RC | 全 P0/P1、8h、包闭环、候选签名 | 真实宿主矩阵、DPI、睡眠/锁屏 |
| Final tag | 先校验 Host/TSF-App 8h 与 P0 真实宿主机器 Gate，再做干净构建、正式签名、包闭环、证据上传与公开资产回下载哈希 | Release Owner 复核 |

CI 必须上传：CTest `LastTest.log`、统一 `result.json`、artifact manifest、JUnit、资源 CSV/summary、签名清单、包闭环 summary 和 ZIP SHA-256。

## 16. Crash/Hang

- Controller 与宿主/Host 分别维护超时和进程存活状态。
- 发生 crash/hang 保存 dump、最近事件环形缓冲、seed、窗口/进程信息、资源曲线和构建身份。
- 随机 run 必须记录 seed 并可 100% 重放。
- Delta debugging 属于第二阶段，不阻塞 MVP。

## 17. 报告格式

`result.json` 是机器判定的单一结果源；JUnit 供 CI，CSV 供资源/延迟分析，HTML 只作为同一 JSON 派生的人读视图，不能拥有独立结论。最小结构：

```json
{
  "schema_version": 1,
  "run_id": "...",
  "build": {"version": "...", "build_id": "...", "commit": "...", "dirty": false},
  "environment": {"windows": "...", "host": "...", "dpi": "..."},
  "cases": [
    {"case_id": "...", "status": "PASS", "duration_ms": 0, "failure_class": "", "evidence": []}
  ],
  "metrics": {},
  "resources": {},
  "summary": {"pass": 0, "fail": 0, "blocked": 0, "not_run": 0}
}
```

报告生成器不得把 `BLOCKED`/`NOT RUN` 计入 PASS。

## 18. 实施路线与当前状态

### Phase A：统一证据（部分完成）

1. build ID、版本、Host health、soak CSV/summary 和包闭环证据已有分散实现。
2. `compose-test-result.ps1` 已落地 schema v1、run ID、构建身份、Case adapter、严格状态汇总和逐文件 SHA-256 artifact manifest；`PASS/FAIL/BLOCKED/NOT_RUN/N/A` 分开计数，通过率只使用实际执行的 `PASS+FAIL`。非法状态、重复 Case ID 和缺失 artifact 失败路径均已进入源码回归。
3. Windows workflow 已在 `always()` 收尾调用统一聚合器；`build.ps1 -TestReportPath` 生成 CTest JUnit，package closure 在失败时也写 `status/stage/error` summary，统一结果及 manifest 会随成功/失败证据上传并进入 tag Release 资产。在线成功 run、PowerShell adapter 的更多原生格式和 HTML 派生视图仍待验证/建设。

### Phase B：Smart Punctuation（核心子集完成，规范覆盖未完）

1. Pure engine、基础规则表、TSF/Scintilla context snapshot、单符号 provisional composition 和 reason code 已落地。
2. Notepad++ 新 DLL 核心矩阵已通过。
3. URL/Email/Path/File、严格数值、技术括号/引号和 provisional lifecycle 的规则与受控 Oracle 已完成；四类 token 退出后立即恢复中文及百分号后中文逗号的 L0/物理键 Oracle 也已加入。当前候选真实 profile 实跑及 ChatGPT/Chromium/Office/VS Code 同构建矩阵仍待完成。

### Phase C：Controlled Host（执行器已建立，当前候选待实跑）

1. Win32 TestHost 已建立：两个普通编辑框、password、PIN、selection 和控件销毁重建，自检已进入 CTest。
2. 物理扫描码 SendInput、focus/context、profile 激活恢复和实际 DLL 模块身份 controller 已接入；非 PiInput fixture 模式通过。
3. Smart Punctuation、Backspace/Esc、跨 context/销毁及 Password/PIN 最终文本 Oracle 已接入，待安装当前候选后执行真实 profile smoke。

### Phase D：长时与发布（进行中/受外部条件阻塞）

1. 冻结候选提交 `a2d5f8fe3c53`、build ID `0.7.13+a2d5f8fe3c53` 的 Host-only 8h 已运行满时长并通过：957 样本，mmap 40,758,365 bytes，Private/WS/Handle 增量和斜率均在阈值内；Host-only Gate 已关闭。
2. TSF/App 持续控制器、资源采样器和 fixture smoke 已完成；当前候选真实 smoke/8h 待执行。
3. 未签名包静态闭环已通过；安装/卸载主流程为当前用户 `asInvoker`，窄范围 UAC 只处理机器 profile/category 与 HKLM COM，用户数据和 Host 保持原用户令牌；实际闭环仍受正式证书和标准用户/应用控制干净机证据阻塞。
4. WPF/WinUI/Qt 与更多真实应用待扩展。

## 19. 推荐目录

```text
tests/piinput_test/
  controller/
  adapters/
    ctest/
    process/
    controlled_host/
    real_app/
  injector/
  oracle/
  metrics/
  watchdog/
  schema/
  scenarios/
    smart_punctuation/
    basic/
    focus/
    switch/
    privacy/
    package/
    soak/
  hosts/
    win32/
  reports/
```
