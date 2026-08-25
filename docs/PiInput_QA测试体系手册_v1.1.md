# PiInput QA 测试体系手册

> QA System Handbook · v1.1  
> 更新日期：2026-08-23

## 修改记录

| 版本 | 修改内容 | 日期 |
|---|---|---|
| v1.0 | 将测试规范、用例、自动化设计和报告模板合并归档 | 2026-08-23 |
| v1.1 | 改为 QA 总入口和治理手册；不再复制子文档正文；纳入 Smart Punctuation 与当前实现成熟度；恢复 Word 初稿稳定 Case ID 语义和遗漏的质量/Fuzz/资源字段 | 2026-08-23 |

## 1. 手册定位

本手册回答四个问题：

1. 规范由哪些文件组成；
2. 开发、测试和发布分别按什么顺序执行；
3. 当前实现真正成熟到哪里；
4. 证据、缺陷、版本和文档如何保持可追溯。

详细规则不在本手册重复，避免多份副本长期漂移。

## 2. 规范集

| 编号 | 文档 | 权威范围 |
|---|---|---|
| 01 | [测试规范与发布门禁 v1.2](PiInput_01_测试规范与发布门禁_v1.2.md) | Gate、证据等级、指标和发布结论 |
| 02 | [测试用例集 v1.1](PiInput_02_测试用例集_v1.1.md) | 稳定 Case ID、步骤、预期和当前覆盖状态 |
| 03 | [PiInputTest 自动化设计 v1.1](PiInput_03_PiInputTest自动化测试程序设计_v1.1.md) | 执行器、事件、Oracle、CI、soak 和实施路线 |
| 04 | [版本测试报告模板 v1.1](PiInput_04_版本测试报告模板_v1.1.md) | 每个 RC/Final 的证据和签字 |
| 05 | [智能符号与中英文标点语义规则 v1.1](PiInput_05_智能符号与中英文标点语义规则_v1.1.md) | Smart Punctuation 产品语义与实现契约 |

Word v1.0/v1.1 文件作为初稿归档；上述 Markdown 是后续工程执行基线。

## 3. 单一事实来源

| 内容 | 单一事实来源 |
|---|---|
| 产品标点语义 | 05 |
| 发布门槛 | 01 |
| Case ID 和预期 | 02 |
| 自动化结构/事件 schema | 03 |
| 某一版本是否通过 | 填写后的 04 |
| 实际能力 | 当前源码 + 注册测试 + 运行证据 |
| 当前版本号 | `VERSION` + 构建产物 `--version` |
| 当前构建身份 | Git commit/dirty + `--build-id` |

计划文档、release notes 和历史报告不能覆盖当前源码事实。文档与源码冲突时，先把冲突记录为缺陷，再决定修改产品还是修改规范；不能悄悄选择方便的一方。

## 4. 当前成熟度仪表板

截至 2026-08-23：

| 领域 | 成熟度 | 说明 | 下一完成条件 |
|---|---|---|---|
| 核心拼音/候选/Host | 可持续回归 | 当前标准 Release 构建及 65/65 已通过；Controlled TSF Host 与 UAC manifest 自检已纳入 | 冻结候选再次 clean 重跑并归档 |
| 结构化语料/专业词 | 已实现 | 313 条总语料，59 条专业词；13 个缺口已补数据 | 固定候选构建报告归档 |
| 敏感输入 | 组件级完成 | InputScope 策略与 TSF 旁路已实现；Controlled TSF Host 显式发布 Password/Numeric PIN，真实 profile Oracle 已写 | 当前候选受控实跑及浏览器/WinUI/凭据矩阵 |
| 大词库 mmap | 已实现，Host 8h 运行中 | Host/benchmark 可报告映射状态；短时稳态通过 | 完整 8h/24h 曲线与真实 TSF soak |
| Smart Punctuation | 核心实现继续完善 | 严格数值、URL/Email/Path/File、技术中缀/边界、千位三位 provisional，数字后第一点立即 ASCII/第二点中文，Scintilla 文档真值、reason code 与受控物理键夹具已落地 | 当前候选 Controlled TSF smoke/直接 lifecycle、`1.文本`/`1.。` 真实宿主复验、引号与单位等剩余规则及跨宿主同构建矩阵 |
| Windows CI | 配置存在 | build/CTest + JUnit/词库、tag/clean 身份、签名、失败证据、包闭环、统一 `result.json`/artifact manifest、Release 回下载 workflow 已写；安装/卸载改为普通用户 `asInvoker` 并加入无辅助产品进程与置顶窗口门禁 | 标准用户和应用控制环境的在线 run 成功并保存 artifacts |
| 签名 | 工具存在、外部阻塞 | 可签名并逐 PE 验证 RFC3161 时间戳，输出 signer/timestamper 指纹与文件哈希；当前无正式证书 | 正式证书 + tag 构建验证 |
| 包闭环 | 静态闭环通过 | 哈希、身份、payload、源码泄漏已实跑；tag 前版下载/哈希、覆盖升级与 UserData 哨兵、安装×2、HKCU/HKLM TSF 与 Host 路径、安装后哈希、受控实际 DLL、卸载残留和可选重装已写 | 受信任签名后在干净用户、Windows 搜索和升级路径实际通过 |
| 8h 稳定性 | 冻结候选 Host-only 通过；TSF/App harness 已写 | `a2d5f8fe3c53` 精确 build 运行满 8h、957 样本，mmap 40,758,365 bytes，Private/WS/Handle 增量和斜率通过；TSF/App 同一宿主循环与 GUI/Host 资源采样已实现 | 当前候选 TSF/App smoke/8h |
| 真实应用 | 部分完成 | Notepad++ 新 DLL 已通过智能标点矩阵；旧 ChatGPT 进程不能代表新构建 | 新构建统一矩阵通过 |

总体结论：当前处于“Smart Punctuation 核心与 Notepad++ 闭环完成，长时、跨宿主、可信签名和普通用户干净机安装证据仍阻断正式候选”的阶段。

## 5. 工作流

### 5.1 需求/缺陷进入

```text
用户现象
  -> 记录宿主/PID/加载版本/按键序列
  -> 绑定或新增 Case ID
  -> 隔离组件日志
  -> 定位边界
  -> 更新规范（若语义不明确）
  -> 修改实现
  -> 自动回归
  -> 真实宿主验收
```

禁止先用一个宿主的表现推导所有 TSF 宿主；也禁止把回调时序补丁当成产品语义。

### 5.2 开发完成定义

一个修改只有满足以下条件才算开发完成：

- 目标语义在对应规范中明确；
- 有稳定 Case ID；
- 最小正确实现完成；
- 相关 L0/L1/L2 测试通过；
- 没有把现有用户修改覆盖掉；
- `git diff --check` 通过；
- 对真实宿主敏感的修改提供可安装构建。

### 5.3 RC 完成定义

- 01 的 G0–G7 全部有实际结果；
- 02 的 P0/P1 全部执行；
- 04 报告无空白硬 Gate；
- 8h、真实宿主、候选签名、升级/卸载均有证据；
- P0/P1=0。

### 5.4 Final 完成定义

- 从干净源码身份构建；
- 正式签名；
- Final 关键 Gate 重跑；
- tag/source/notes/package/hash 对应；
- 公开资产下载复核；
- 04 最终报告归档。

## 6. 证据治理

每次 run 使用唯一目录：

```text
artifacts/qa/<version>/<run_id>/
  build-identity.json
  result.json
  artifact-manifest.json
  junit.xml
  ctest/
  logs/
  metrics/
  package-closure/
  signatures/
  screenshots/
  dumps/
```

最小 `build-identity.json`：

```json
{
  "version": "0.0.0",
  "build_id": "0.0.0+commit",
  "git_commit": "...",
  "git_dirty": false,
  "configuration": "Release",
  "package_sha256": "..."
}
```

真实应用证据至少记录：宿主名/版本、PID、加载的 PiInput DLL 路径或哈希、Windows build、按键序列、expected/actual 和时间。

## 7. 缺陷治理

### 7.1 缺陷最小字段

```text
Bug ID
Priority
Case ID
Build ID
Host/version/PID
Reproduction steps
Expected / Actual
Failure class
Repro rate
Evidence
Root cause boundary
Fix commit
Regression result
```

### 7.2 根因边界

至少区分：

- Input Injector/物理按键；
- TSF `OnTestKeyDown`/`OnKeyDown`；
- context/scope 绑定；
- Shim pipe 排队；
- Host session/engine；
- TSF edit session；
- 宿主文本控件；
- installer/旧进程模块驻留。

共享日志必须按进程隔离，否则不能用于跨宿主时序结论。

### 7.3 回归要求

- 每个 P0/P1 修复至少新增一个自动用例；若当前只能人工，则先新增 MANUAL Case ID 和可复现步骤。
- 回归必须先证明修复，再证明没有破坏邻近语义。
- 标点修复至少包含 ASCII 正例、中文正例、快速输入、Backspace/Esc 和 context 切换。

## 8. Smart Punctuation 专项治理

Smart Punctuation 已从“待建纯引擎”进入“核心子集完成、完整规范补齐与跨宿主验收”阶段：

1. 纯决策引擎、真实 context 快照、单符号 provisional composition、`/`/`、` 物理键策略和 reason code 已落地；严格数值、URL/Email/Path/File 和技术中缀的 L0 分类也已补齐，不再回到“上一键是数字”的回调状态补丁。
2. Notepad++ 新 DLL 核心矩阵已通过；它只证明该宿主和该子集，不能代替完整 `SP-MIX-*` 或其他宿主。
3. Controlled TSF Host 的物理扫描码、焦点/context Oracle 已通过；真实 PiInput profile、实际 DLL 身份、千位/技术符号、数字后中文问号、token 退出恢复中文、百分号后中文逗号、Backspace/Esc、跨 context/销毁和 Password/PIN smoke 已实现但尚未用当前候选安装执行。句号不再 provisional：第一下立即 ASCII，第二下中文；数字 provisional 仅保留 `:`/`,`，不会吞掉普通 `？/！`。下一边界是执行 lifecycle smoke、`1.文本`/`1.。` 与跨宿主验收。
4. Notepad++、ChatGPT、Chromium、Office、VS Code 必须加载同一 build identity；旧进程加载旧 DLL 的表现不计入候选结果。
5. 只有 05 的完整 P0 规则族、状态/隐私用例和跨宿主矩阵全部通过，G4 才能从 `BLOCKED` 改为 `PASS`。

## 9. 稳定性与资源治理

资源结论分三类，不得混用：

| 类型 | 说明 |
|---|---|
| Smoke | 分钟级，验证 harness、采样和明显泄漏 |
| Host-only Soak | 真实 Host + pipe + 大 mmap 词库，不经过 TSF 宿主 |
| TSF/App Soak | 真实或受控宿主，包含 context/focus/IME/candidate UI |

正式 RC 要求两类 8h soak 均完成。报告必须同时展示增长量和斜率，不能只比较开始/结束两个点。

## 10. CI 与签名治理

- PR workflow 可以在无证书时执行未签名开发包闭环。
- tag workflow 缺少 signing secret 必须失败。
- PFX Base64/密码 secret 只暴露给单个签名步骤，不进入 job 级环境；临时 PFX 在该步骤 `finally` 删除且绝不上传。
- `RequireSigned` 由 tag/final policy 决定，不能由开发者为通过测试随意关闭。
- 正式资产必须再次验证每个 PE 签名和 ZIP SHA-256。
- 安装闭环必须核对 HKCU 与 SearchHost 可见的 HKLM TSF 路径、Host 路径、包内/安装后 Host 与 TSF 哈希，并用 Controlled TSF Host 证明实际加载 DLL；普通桌面宿主核对 popup `GW_OWNER`，Windows 搜索核对 UIElement 候选由宿主集成显示且外部窗被抑制，不能只用最终汉字上屏代替 UI 证据；tag 发布后再次下载 ZIP，与 sidecar 和本地 SHA-256 三方比对。
- CI 失败也必须保存证据：CTest 输出 JUnit，package closure 在最早的哈希失败到安装/卸载失败之间都写出 `status=failed`、`stage` 和错误原因。
- CI 的 `always()` 收尾使用 `compose-test-result.ps1` 生成 schema v1 统一结果和 SHA-256 manifest；非法状态、重复 Case ID 或声明证据缺失必须失败，`BLOCKED/NOT_RUN/N/A` 只单独计数，不进入 executed pass rate。
- 正式 tag 在任何构建/签名前先执行 `verify-release-evidence.ps1`；Host-only 8h、TSF/App 8h、P0 真实宿主矩阵必须在对应版本验证记录中全部标为 `PASS`，否则流水线 fail-closed。
- 包闭环必须使用 `VERSION+git short SHA(12)` 精确校验当前 tag 的 Host build ID；只匹配版本前缀不足以证明源码、tag 和资产同一。

## 11. 文档变更规则

- 语义改变：先更新 05，再改 02 的 Case，最后实现。
- Gate 改变：更新 01 和 04；QA 手册只更新治理摘要。
- 自动化架构改变：更新 03；02 只调整 Current/Mode/Layer。
- Case ID 一经发布不得复用为不同语义；废弃时保留并标记 deprecated。
- Word 初稿不继续并行维护，避免双写漂移；需要对外发布时可从 Markdown 生成 Word/PDF。

## 12. 接下来按文档实施的顺序

1. 冻结候选 `a2d5f8fe3c53` 的 Host-only 8h 已核对通过；保持该二进制身份，继续 TSF/App 与真实宿主 Gate。
2. 在已完成严格 token、千位 provisional、技术符号和受控物理键/lifecycle Oracle 基础上，安装当前候选并完成真实 profile smoke。
3. 用同一 build identity 完成 Smart Punctuation、敏感输入和基础状态的 P0 真实宿主矩阵。
4. 完成全部修改后的干净 Release build/全 CTest，固化 313/59 语料、mmap 和构建身份结果。
5. 用已建立的 `tsf_app_soak_tests.ps1` 先做当前候选短时 smoke，再跑满 TSF/App 8h，随后完成真实应用、DPI、锁屏/睡眠和焦点/IME/Context 压力矩阵。
6. 取得正式证书，执行已接入的 signed package closure、干净用户安装×2、自动前版升级/UserData 哨兵、卸载和重启复核。
7. 填写 04；只有所有硬 Gate 有实际结果、P0/P1=0 后才进入 Final 和公开资产闭环。

## 13. 六文档追踪矩阵

| 需求族 | 规范/Gate | 稳定 Case ID | 自动化/执行设计 | 报告落点 | 当前结论 |
|---|---|---|---|---|---|
| 构建身份与回归 | 01 G0/G1 | `BUILD-001..008` | 03 L0–L2、CI | 04 §1/2/4/5 | 当前 Release 65/65；统一 result/manifest 已接入，冻结候选 clean 重跑与在线 run 待完成 |
| 输入完整性/状态机 | 01 G2 | `IME-BASIC-*`、`IME-FOCUS-*`、`IME-SW-*` | 03 Controller/Oracle/Controlled Host | 04 §5/6/9 | 组件较完整，受控/真实宿主压力未完 |
| 敏感输入 | 01 G3 | `PRIV-001..006`、`SP-PRIV-001` | 03 scope event/Oracle | 04 §7/8 | 策略已实现，真实输入框矩阵未完 |
| 智能标点 | 01 G4、05 | `SP-MIX-001..009`、`SP-STATE-*`、`SP-HOST-*` | 03 §8、Controlled Host | 04 §7 | 核心子集和 Notepad++ 完成，完整规范/跨宿主未完 |
| 真实应用/DPI/生命周期 | 01 G5 | `IME-APP-*`、`IME-DPI-*`、`IME-OS-*` | 03 L3–L5 | 04 §9/13 | 大部分 MANUAL/PLANNED |
| 词库/中文质量 | 01 G1/G6.3 | `LEX-*`、`IME-QUAL-*` | 03 corpus/metrics | 04 §5/11/12 | 313/59 确定性回归已完成；完整质量基线未完 |
| 性能/资源/长时 | 01 G6 | `IME-PERF-*`、`IME-RES-*` | 03 §12–14 | 04 §10/13 | 冻结候选 Host-only 8h 已通过；UI 延迟与 TSF/App soak 未完 |
| 安装/签名/包 | 01 G7 | `IME-INST-*`、`IME-SIGN-*`、`IME-PKG-001` | 03 CI/Release adapter | 04 §2/4/14 | 普通用户静态闭环通过，路径/哈希/实际 DLL/时间戳门禁已写；证书/标准用户与应用控制实跑/升级路径阻塞 |
| 发布资产 | 01 G8 | `BUILD-005..007` | 03 Final tag pipeline | 04 §2/3/4/18 | 前置 Gate 未过，尚未执行 |

## 14. 初稿保留与文档完成检查

| 文档 | 对 Word 初稿的处理 | 本版新增校准 |
|---|---|---|
| 01 | 保留兼容性、完整性、状态机、性能、中文质量、压力资源、安装和缺陷门禁 | 增加证据等级、隐私、签名包闭环和当前真实状态 |
| 02 | 保留所有初版 Case ID 原语义；补回 `FOCUS-006`、`QUAL-*`、`EDGE-*` | 新场景只追加 ID；拆分覆盖状态与实际执行结果 |
| 03 | 保留事件关联、分段延迟、Fuzz、Crash/Hang、CI、MVP 和目录设计 | 改为复用现有 63 项测试与 Host/PowerShell 资产，标注各 Phase 当前状态 |
| 04 | 保留兼容、事件规模、性能、完整资源和中文质量表 | 增加 build identity、智能标点、隐私、mmap、双 soak、签名和包闭环 |
| 05 | 保留全部符号语义与 `SP-MIX-001..009` 原语义 | 增加宿主回调根因、provisional 状态、隐私边界及规范/实现差距矩阵 |
| QA | 不再复制 01–04 正文，避免多份真相 | 作为单一入口维护权威边界、流程、追踪矩阵和实施顺序 |

本轮六份 Markdown 的“文档完成”指：原初要求均有归属、稳定 ID 不漂移、交叉链接有效、当前实现不被夸大、未执行项保留为 `BLOCKED/NOT RUN/PLANNED`。它不等于产品 Release Gate 已完成。

## 15. 评审检查表

- [ ] 规范、Case、实现和报告是否使用同一语义？
- [ ] 当前状态是否把“存在”误写成“通过”？
- [ ] PASS 是否绑定构建身份和证据？
- [ ] P0/P1 是否为 0？
- [ ] BLOCKED/NOT RUN 是否被错误计入通过率？
- [ ] 真实宿主是否加载了本次候选 DLL？
- [ ] 敏感范围是否有真实输入框证据？
- [ ] Smart Punctuation 是否同时保留 ASCII 和中文标点？
- [ ] 8h 是否真的运行满 8 小时？
- [ ] 包内 PE 是否全部正式签名并验证？
- [ ] tag/source/notes/package/hash 是否一一对应？
