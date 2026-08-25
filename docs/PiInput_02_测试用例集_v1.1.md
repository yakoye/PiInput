# PiInput 测试用例集

> Manual + Automated Test Cases · v1.1  
> 更新日期：2026-08-23

## 修改记录

| 版本 | 修改内容 | 日期 |
|---|---|---|
| v1.0 | 初版 | 2026-08-23 |
| v1.1 | 按当前自动化能力重分状态；增加智能标点、敏感输入、构建身份、内存映射、签名包闭环和 8h 曲线用例 | 2026-08-23 |

## 1. 使用规则

每个缺陷修复必须绑定稳定 Case ID。用例执行结果只允许：

- `PASS`：在指定构建、宿主和步骤上实际通过；
- `FAIL`：实际执行但不符合预期；
- `BLOCKED`：缺少证书、时间窗、应用、权限或环境；
- `NOT RUN`：有条件但尚未执行；
- `N/A`：经说明不适用。

不得把“源码中存在测试”“CTest 已注册”或“脚本能解析”填写为运行通过。

## 2. 字段

| 字段 | 含义 |
|---|---|
| ID | 稳定唯一编号，用于缺陷、日志、CI 和报告关联 |
| P | P0/P1/P2/P3 |
| Mode | `AUTO`、`MANUAL`、`AUTO+MANUAL` |
| Layer | L0 单元、L1 组件、L2 受控进程/宿主、L3 真实应用、L4 系统/长时 |
| Current | `COVERED`、`PARTIAL`、`PLANNED`、`MANUAL`、`BLOCKED`（缺少正式证书等外部条件） |
| Evidence | 执行后填写命令、日志/报告路径和构建身份 |

`Current=COVERED` 只表示存在稳定自动化入口，不表示当前修改后的构建已经 PASS。

已在 Word v1.0 出现的 Case ID 是兼容接口，不得因重排表格而改变语义。本版保留原 ID；新增场景只追加新 ID。历史缺陷、日志和报告若引用旧 ID，必须仍能指向同一测试意图。

## 3. 当前 CTest 与用例映射

当前 Release 配置注册 65 个 CTest；受控 TSF Host 与安装/卸载 UAC manifest 自检均已纳入，全量 65/65 已通过。关键映射如下：

| 自动化入口 | 主要覆盖 |
|---|---|
| `piinput-core-tests` | 拼音、词典、标点映射、引擎基础 |
| `piinput-host-session` | Host 输入模式、候选、提交、基础标点 |
| `piinput-composition-mirror` | 请求/回复、最终编辑顺序和 composition mirror |
| `piinput-input-scope-policy` | 敏感 InputScope 分类策略 |
| `piinput-controlled-tsf-host-self-test` | 两个普通编辑框、密码/PIN、selection、清空和控件销毁重建 |
| `piinput-controlled-tsf-controller` | 物理扫描码、焦点隔离、context 重建；可选真实 PiInput profile 模式校验智能标点、敏感旁路和实际加载 DLL 路径 |
| `piinput-host-process` | 真实 Host 进程、health、协议、版本/build ID、性能烟雾 |
| `piinput-structured-corpus-regression` | 结构化语料与 59 条专业词 |
| `piinput-windows-source-regression` | Windows 源码政策和关键结构 |
| `piinput-installer-layout-tests` / `piinput-uninstall-layout-tests` | 安装/卸载布局逻辑 |
| `piinput-performance-smoke` / external performance | 引擎/大词库受控性能 |

这些测试没有覆盖完整真实 TSF 宿主矩阵，也没有替代签名、安装包闭环和 8h 运行。

## 4. 构建、版本与产物

| ID | P | Mode | Layer | Current | 步骤 | 预期 |
|---|---|---|---|---|---|---|
| BUILD-001 | P0 | AUTO | L2 | COVERED | 当前 Release 构建和全量 65/65 CTest 已通过；CI `always()` 汇总统一 `result.json`，正式发布仍要求冻结候选 clean 重跑 | 构建成功；Failed=0；skip/disabled 有解释；统一结果绑定 build ID |
| BUILD-002 | P0 | AUTO | L2 | COVERED | 比较 `VERSION`、Host `--version`、包名 | 完全一致 |
| BUILD-003 | P0 | AUTO | L2 | COVERED | 读取 Host `--build-id`；CI package closure 精确比较 `VERSION+12位 tag commit`，错误 ID 失败夹具已通过 | 含版本与源码身份；与 tag commit 精确一致 |
| BUILD-004 | P0 | AUTO | L2 | PARTIAL | workflow 已加入 tag/VERSION 与 checkout dirty 检查，待在线执行 | dirty 构建或 tag/version 不一致被正式流程拒绝 |
| BUILD-005 | P0 | AUTO | L2 | PARTIAL | 闭环脚本比较 ZIP、解包、安装后 Host/TSF PE 哈希，待普通用户干净机实跑 | 同一构建文件一致 |
| BUILD-006 | P1 | AUTO | L2 | COVERED | 检查包内禁止扩展名 | 无源码、脚本、PDB、map、工程文件泄漏 |
| BUILD-007 | P1 | AUTO | L2 | COVERED | 校验 ZIP SHA-256 sidecar；`compose-test-result.ps1` 对输入证据生成逐文件 SHA-256 artifact manifest，非法状态、重复 Case ID、缺失 artifact 夹具均 fail-closed | 哈希一致，格式可机器解析；证据清单无重复或悬空文件 |
| BUILD-008 | P0 | AUTO | L2 | PARTIAL | tag workflow 调用 `verify-release-evidence.ps1`，要求 Host/TSF-App 8h 与 P0 真实宿主矩阵均为 PASS；PASS/NOT_RUN fixture 已验证，待在线 run | 外部 Gate 未完成时不构建、不签名、不发布正式 tag |

## 5. 安装、升级、卸载和签名

| ID | P | Mode | Layer | Current | 场景 | 预期 |
|---|---|---|---|---|---|---|
| IME-INST-001 | P0 | AUTO+MANUAL | L3 | PARTIAL | 干净用户全新安装；核对 Program Files 受保护 Shim、HKCU/HKLM COM、profile/category 和键盘列表 | profile 可见、可激活；桌面与 SearchHost 均加载同一受保护 DLL；版本正确 |
| IME-INST-002 | P0 | AUTO+MANUAL | L3 | PARTIAL | 静默卸载并检查 | 无失效 profile、卸载项和孤儿运行时 |
| IME-INST-003 | P1 | AUTO+MANUAL | L3 | PARTIAL | tag CI 自动下载最近正式版 ZIP/sidecar，安装前版并写 UserData 哨兵后升级当前候选；待在线实跑 | 运行时替换；配置/词库按策略保留；旧 DLL 不残留 |
| IME-INST-004 | P1 | AUTO+MANUAL | L3 | PLANNED | RC 到 Final | Final 身份唯一，无 RC 残留或双 profile |
| IME-INST-005 | P0 | AUTO+MANUAL | L2/L3 | BLOCKED | 历史“二进制签名验证”用例；执行 `IME-SIGN-001/002` 并汇总结论 | 安装包及核心 PE 签名有效、证书链可信、无测试证书 |
| IME-INST-006 | P1 | AUTO+MANUAL | L3 | PARTIAL | 同版本二次覆盖安装 | 幂等，无双 profile |
| IME-INST-007 | P1 | MANUAL | L4 | MANUAL | 安装/卸载后重启 | 开机后状态与安装策略一致 |
| IME-SIGN-001 | P0 | AUTO | L2 | BLOCKED | `signtool verify /pa /all` 检查包内 PE | 全部有效，证书链可信 |
| IME-SIGN-002 | P0 | AUTO | L2 | BLOCKED | 脚本已强制检查 RFC3161 时间戳证书并输出 signer/timestamper 指纹，待正式证书实跑 | SHA-256 + RFC3161 时间戳 |
| IME-SIGN-003 | P0 | AUTO | L2 | PARTIAL | tag 缺 signing secret 的 fail-closed workflow 已写，待在线验证 | 流水线失败，不生成正式候选 |
| IME-PKG-001 | P0 | AUTO | L3 | BLOCKED | 主流程为当前用户 `asInvoker`；窄范围 UAC 只注册/反注册机器 profile/category 与 HKLM COM。闭环已同时核对 HKCU/HKLM TSF 路径、Host/TSF 哈希、受控实际 DLL、卸载残留和可选重装；待受信任签名候选在标准用户和应用控制机器实跑 | 哈希、payload、身份、安装×2、Windows 搜索、受控输入、卸载均通过 |

## 6. 基础输入与状态机

| ID | P | Mode | Layer | Current | 步骤 | 预期 |
|---|---|---|---|---|---|---|
| IME-BASIC-001 | P0 | AUTO+MANUAL | L2/L3 | PARTIAL | 中文 `nihao`、英文 `abc`、再中文 | 模式和输出一致 |
| IME-BASIC-002 | P0 | AUTO | L2 | PARTIAL | 100k 确定性/随机按键 | Lost/Duplicate/Unexpected/Wrong=0 |
| IME-BASIC-003 | P0 | AUTO+MANUAL | L1/L3 | PARTIAL | 中文模式数字/中英文标点混输；细分由 `SP-*` 承接 | 输出符合 05，无吞键或无法获得 ASCII 本义 |
| IME-BASIC-004 | P1 | AUTO+MANUAL | L2/L3 | COVERED | composition 中 Backspace | 文本、caret、候选同步 |
| IME-BASIC-005 | P1 | AUTO+MANUAL | L2/L3 | PARTIAL | Left/Right/Home/End 后编辑 | 不越界、不乱序 |
| IME-BASIC-006 | P1 | AUTO+MANUAL | L2/L3 | COVERED | Esc 取消 | composition/candidate 清除，回 Idle |
| IME-BASIC-007 | P0 | AUTO+MANUAL | L2/L3 | COVERED | Enter 提交 | 内容正确，只提交一次 |
| IME-BASIC-008 | P0 | AUTO+MANUAL | L2/L3 | COVERED | Space 选首候选 | 只提交一次 |
| IME-BASIC-009 | P1 | AUTO+MANUAL | L2/L3 | COVERED | 数字键选词 | 对应候选正确 |
| IME-BASIC-010 | P2 | AUTO+MANUAL | L2/L3 | COVERED | 候选翻页/折叠 | 无漏项、重复和死键 |
| IME-BASIC-011 | P0 | AUTO | L2 | COVERED | 连续两个会提交的标点快速输入 | 最终编辑严格串行 |
| IME-BASIC-012 | P0 | AUTO+MANUAL | L2/L3 | PARTIAL | 粘贴后立即输入 | 不覆盖粘贴文本，不复活旧 composition |
| IME-BASIC-013 | P1 | AUTO+MANUAL | L2/L3 | PARTIAL | Host 重启/升级 handoff | 会话恢复或安全取消，无重复提交 |
| IME-BASIC-014 | P0 | AUTO+MANUAL | L2/L3 | PARTIAL | 新获焦点或新建 context 后立即连续输入 `123`；覆盖 Chrome 搜索框、远程桌面地址框和 Win32 Edit。L2 已区分 Resume 同步请求与真实按键请求，并有源码门禁；L3 待同构建实机验收 | 空闲状态三个数字全部由宿主上屏；不得把 Resume pending 当成候选态而吞首键或全部数字 |

## 7. 智能标点

完整规则见 `PiInput_05`。`Current` 按单条用例的自动化入口填写；即使某条达到 `COVERED`，也不能在其余 P0 规则族或跨宿主矩阵未完成时把整个 Smart Punctuation Gate 写成 PASS。Word v1.0 已定义的 `SP-MIX-001..009` 语义保持不变。

| ID | P | Mode | Layer | Current | 目标文本/操作 | 预期 |
|---|---|---|---|---|---|---|
| SP-MIX-001 | P0 | AUTO+MANUAL | L1/L3 | PARTIAL | `1.明天干嘛。`；组件和 Notepad++ 的 `1.` 已通过，整句同构建矩阵待验收 | 同句出现序号 ASCII `.` 与中文 `。` |
| SP-MIX-002 | P0 | AUTO+MANUAL | L1/L3 | PARTIAL | `这个是2/3，那么我们选白色/黑色，白色、黑色。` | 分数/选项 `/`、列举 `、` 和中文标点共存 |
| SP-MIX-003 | P0 | AUTO+MANUAL | L1/L3 | PARTIAL | 输入 `会议时间：12:23，版本是v1.2.3..`；另断言 `1.文本` 不回写、`版本1？/！` 不进入数字 provisional | 上屏 `会议时间：12:23，版本是v1.2.3.。`；第一点 ASCII、第二点中文，数字不污染其他中文标点 |
| SP-MIX-004 | P0 | AUTO+MANUAL | L1/L3 | PARTIAL | `价格是1,299.50元，折扣为80%。`；`折扣80%，` 连续物理键 Oracle 已编译，待当前候选实跑 | 千位逗号、小数点、百分号与中文标点共存 |
| SP-MIX-005 | P0 | AUTO+MANUAL | L1/L3 | PARTIAL | `服务器是192.168.1.1，端口是8080。` | IPv4 内部点 ASCII，句末中文 |
| SP-MIX-006 | P0 | AUTO+MANUAL | L1/L3 | PARTIAL | 技术方括号/圆括号/下划线、位域冒号的 L0 规则和 Controlled TSF Oracle 已加入，当前候选实跑待完成 | 技术方括号/冒号不中文化 |
| SP-MIX-007 | P0 | AUTO+MANUAL | L1/L3 | PARTIAL | `支持Windows/Linux/macOS，选择是/否即可。` | 中文句子中的 `/` 保持本义 |
| SP-MIX-008 | P0 | AUTO+MANUAL | L1/L3 | PARTIAL | URL scheme/dot/query 及退出 URL 后恢复中文问号的 L0 已通过，Controlled TSF 连续 Oracle 已编译；当前候选与跨宿主待验收 | URL token 全 ASCII，离开后恢复中文 |
| SP-MIX-009 | P0 | AUTO+MANUAL | L1/L3 | PARTIAL | Email/文件名/版本/Path/Code 内部规则及退出四类 token 后恢复中文的 L0 已通过，Controlled TSF Oracle 已编译；当前候选与跨宿主待验收 | token 内部符号保护，随后立即恢复中文 |
| SP-MIX-010 | P0 | AUTO+MANUAL | L1/L3 | PARTIAL | 中文状态输入 `以万物为刍狗;圣人不仁。`；L1 Host 与 Controlled TSF 物理键 Oracle 已加入，当前候选实跑待完成 | 单次 `;` 立即上屏 `；`；不得进入 `;;f`、`;;u` 或 `;关键词` 命令状态 |
| SP-STATE-001 | P0 | AUTO | L1/L2 | COVERED | `1` `.` `0` 快速连打；pending 数字解析和异步串行已有测试/源码门禁 | 输出 `1.0`，顺序不乱 |
| SP-STATE-002 | P0 | AUTO | L1/L2 | PARTIAL | 千位 provisional 已实现逐位回退且 Esc 保留数字，Controlled TSF 物理键 Oracle 已写，待当前候选实跑 | 只取消临时符号，不吞后续数字 |
| SP-STATE-003 | P0 | AUTO+MANUAL | L2/L3 | PARTIAL | context/session 所有权校验已实现；受控跨输入框和销毁重建 Oracle 已写，待当前候选实跑及真实宿主补充 | 无跨 context 回写 |
| SP-STATE-004 | P1 | AUTO | L1/L2 | PARTIAL | 决策层覆盖真实右侧文本；Controlled TSF 真实 profile smoke 已写入中间插入 Oracle，待当前候选实跑 | 根据真实左右文本即时决定 |
| SP-PRIV-001 | P0 | AUTO+MANUAL | L1/L3 | PARTIAL | Controlled TSF Host 显式发布 Password/Numeric PIN scope，真实 profile smoke 已写入物理键旁路 Oracle，待当前候选实跑 | 完全旁路，不读取上下文、不进 Host、不记录文本 |
| SP-HOST-001 | P0 | MANUAL | L3 | MANUAL | Notepad 与 Notepad++ 同序列 | 完全一致 |
| SP-HOST-002 | P0 | MANUAL | L3 | MANUAL | ChatGPT/Chrome/VS Code 同序列 | 无宿主特有回退 |

## 8. 敏感输入与日志隐私

| ID | P | Mode | Layer | Current | 场景 | 预期 |
|---|---|---|---|---|---|---|
| PRIV-001 | P0 | AUTO | L1 | COVERED | Password scope | 判定为敏感 |
| PRIV-002 | P0 | AUTO | L1 | COVERED | PIN/numeric-password/private scope | 判定为敏感 |
| PRIV-003 | P0 | AUTO+MANUAL | L2/L3 | PARTIAL | 普通框切密码框且复用 context | 下一键立即旁路 |
| PRIV-004 | P0 | AUTO+MANUAL | L2/L3 | PARTIAL | 密码框切回普通框 | 普通输入恢复，不携带旧状态 |
| PRIV-005 | P0 | AUTO | L2 | PARTIAL | 开启诊断日志输入敏感字段 | 无文本、候选、Host 请求、学习记录 |
| PRIV-006 | P0 | MANUAL | L3 | MANUAL | 浏览器、Win32、WinUI、凭据类输入框 | 全部旁路且宿主可输入 |

## 9. 焦点、context 与输入法切换

| ID | P | Mode | Layer | Current | 场景 | 预期 |
|---|---|---|---|---|---|---|
| IME-FOCUS-001 | P0 | AUTO+MANUAL | L2/L3 | PARTIAL | composition 中 Alt+Tab 往返 | 按策略结束，无残留 |
| IME-FOCUS-002 | P0 | AUTO+MANUAL | L2/L3 | PARTIAL | 同窗口两输入框切换 | context 隔离，立即可输入 |
| IME-FOCUS-003 | P0 | AUTO | L2 | PARTIAL | composition 中销毁窗口 | 不 crash、不幽灵回写 |
| IME-FOCUS-004 | P0 | AUTO | L4 | PLANNED | 10k 焦点切换 | 0 卡状态/丢键，资源回稳态 |
| IME-FOCUS-005 | P1 | AUTO+MANUAL | L3 | MANUAL | 新建/关闭标签页 | 文档会话隔离 |
| IME-FOCUS-006 | P1 | AUTO+MANUAL | L3 | MANUAL | composition 中打开/关闭弹窗或对话框 | 状态不乱；返回后继续/重输符合策略 |
| IME-SW-001 | P0 | AUTO+MANUAL | L3 | PARTIAL | PiInput -> 其他 IME -> PiInput | profile、图标、模式和输入一致 |
| IME-SW-002 | P0 | AUTO | L4 | PLANNED | 10k IME 切换 | 0 切换失败/错模式/crash |
| IME-SW-003 | P1 | AUTO+MANUAL | L3 | MANUAL | composition 中切 IME | 旧 composition 安全结束 |
| IME-SW-004 | P1 | AUTO+MANUAL | L2/L3 | PARTIAL | Shift 快速抖动 1000 次 | 最终状态等于最后有效操作 |

## 10. 真实应用兼容矩阵

每个应用至少执行：中文输入、英文输入、候选选择、Backspace/Esc、数字标点、粘贴后输入、Alt+Tab、切 IME、多输入框/标签页。

| ID | P | Host | Current |
|---|---|---|---|
| IME-APP-001 | P0 | Notepad | MANUAL |
| IME-APP-002 | P0 | Notepad++ | MANUAL；当前数字标点缺陷的首要回归宿主 |
| IME-APP-003 | P1 | Word | MANUAL |
| IME-APP-004 | P1 | Excel | MANUAL |
| IME-APP-005 | P0 | Chrome | MANUAL；增加新获焦点后首个数字不丢失回归 |
| IME-APP-006 | P1 | Edge | MANUAL |
| IME-APP-007 | P0 | VS Code | MANUAL |
| IME-APP-008 | P0 | ChatGPT Windows App | MANUAL |
| IME-APP-009 | P1 | Windows Terminal | MANUAL |
| IME-APP-010 | P1 | PowerShell/CMD | MANUAL |
| IME-APP-011 | P0 | Windows 设置/搜索/WinUI | PARTIAL；SearchHost 已加载 Shim 且可提交汉字，但协议 v4 的跨进程 owned popup 实机仍不可见。根因继续下钻为能力声明与实现不一致：已注册 `UIELEMENTENABLED/IMMERSIVESUPPORT`，Shim 却未实现 UIElement/`ITfTextInputProcessorEx`。当前协议 v5 已补齐 `ITfCandidateListUIElementBehavior`、搜索框集成接口与 `ActivateEx`；宿主接管时抑制 Host 外部窗，普通桌面宿主继续走 owned popup。L0/L2 已覆盖候选数据、分页、选择/取消、v4 兼容与显示通道标志；新构建仍须在 SearchHost 看到系统候选行后才可 PASS |
| IME-APP-012 | P1 | WPF 控件 | MANUAL/PLANNED HOST |
| IME-APP-013 | P1 | Qt 控件 | MANUAL/PLANNED HOST |
| IME-APP-014 | P0 | Windows 远程桌面连接地址框 | MANUAL；增加空闲状态连续数字完全透传回归 |

## 11. DPI、权限和系统生命周期

| ID | P | Mode | Layer | Current | 场景 | 预期 |
|---|---|---|---|---|---|---|
| IME-DPI-001 | P1 | MANUAL | L3 | MANUAL | 100/125/150/200% | 候选清晰、位置正确 |
| IME-DPI-002 | P1 | MANUAL | L3 | MANUAL | 混合 DPI 双屏迁移 | 无跳变、残影、出屏 |
| IME-DPI-003 | P2 | MANUAL | L3 | MANUAL | 屏幕边缘和多行候选 | 自动避让 |
| IME-OS-001 | P1 | AUTO+MANUAL | L4 | PLANNED | 锁屏/解锁循环 | 恢复后立即可输入 |
| IME-OS-002 | P1 | AUTO+MANUAL | L4 | PLANNED | 睡眠/唤醒循环 | profile 与状态一致 |
| IME-OS-003 | P1 | MANUAL | L3 | MANUAL | 普通/管理员窗口切换 | 符合 Windows 安全边界 |

## 12. 词典、语料和内存映射

| ID | P | Mode | Layer | Current | 场景 | 预期 |
|---|---|---|---|---|---|---|
| LEX-001 | P0 | AUTO | L0/L2 | COVERED | 结构化语料 | 总计 313 条，全部通过 |
| LEX-002 | P1 | AUTO | L0/L2 | COVERED | 专业词 | 59 条专业词全部通过 |
| LEX-003 | P1 | AUTO | L0 | COVERED | 13 个已知专业词缺口 | 不再依赖 known-missing 豁免 |
| LEX-004 | P0 | AUTO | L2 | COVERED | 大 `.lex` Host health | `lexicon_storage=mmap` 且映射字节有效 |
| LEX-005 | P1 | AUTO | L0/L2 | PARTIAL | 映射/heap 结果一致性 | 查询和候选一致 |
| LEX-006 | P1 | AUTO | L2 | PARTIAL | 大词库冷/热加载 | 输出分布、条目数、映射状态和内存 |
| LEX-007 | P1 | AUTO | L4 | COVERED | 冻结候选 `a2d5f8fe3c53` 8h 已通过：957 样本、mmap 40,758,365 bytes、Private/WS/Handle 增量与斜率均在阈值内 | 映射保持有效，无增长趋势 |

## 13. 性能与资源

| ID | P | Mode | Layer | Current | 场景 | 预期 |
|---|---|---|---|---|---|---|
| IME-PERF-001 | P1 | AUTO | L3 | PLANNED | Key->Composition 10k key | 输出 P50/P90/P95/P99/MAX；P95 目标 <16 ms |
| IME-PERF-002 | P1 | AUTO | L3 | PLANNED | Key->Candidate | 同上；P95 目标 <30 ms |
| IME-PERF-003 | P1 | AUTO | L3 | PLANNED | Commit | 同上；P95 目标 <20 ms；Lost/Duplicate=0 |
| IME-PERF-004 | P2 | AUTO | L2/L3 | PARTIAL | 冷启动 100 次 | 定义统一冷缓存口径并输出分布；目标 <150 ms |
| IME-PERF-005 | P2 | AUTO | L3 | PLANNED | 大候选集翻页 1000 次 | P95 目标 <30 ms，无冻结 |
| IME-PERF-006 | P1 | AUTO | L0/L2 | COVERED | 引擎/Host 热查询 | 不超过冻结基线阈值；不得冒充 UI 端到端延迟 |
| IME-RES-001 | P0 | AUTO | L4 | PARTIAL | `tsf_app_soak_tests.ps1` 和控制器持续工作负载已实现，待当前候选安装后跑满 8h | Crash/Hang/Lost/Duplicate/Stuck=0；资源无持续泄漏 |
| IME-RES-002 | P1 | AUTO | L4 | PLANNED | TSF/App 24h | 曲线进入稳态，无异常爬升 |
| IME-RES-003 | P1 | AUTO | L4 | PARTIAL | 持续控制器每 20 轮销毁/重建 context 并验证无幽灵文本；状态/CSV/summary 已记录 `context_recreates`，fixture 22 轮/2 次通过，10k 正式规模待跑 | Handle/Thread/GDI/USER 回稳态 |
| IME-RES-004 | P1 | AUTO | L4 | PLANNED | Idle 5 min | CPU 接近零，无高频轮询 |
| IME-RES-005 | P2 | AUTO | L4 | PLANNED | 启动稳定后采样 10 min | 记录 Idle Memory 基线；重点无增长 |
| IME-RES-006 | P0 | AUTO | L4 | COVERED | 冻结候选 Host-only 8h 已通过：build ID `0.7.13+a2d5f8fe3c53`、957 样本、summary 与增长/斜率门禁全部通过 | 运行满 8h，mmap 有效，增长量与斜率及 `summary.json` 通过；不替代 IME-RES-001 |

## 14. 中文输入质量

| ID | P | Mode | Layer | Current | 场景 | 预期 |
|---|---|---|---|---|---|---|
| IME-QUAL-001 | P1 | AUTO | L0/L2 | PARTIAL | 冻结词库/设置跑固定语料 Top1 | 输出总体及类别 Top1；313 条结构化语料已有确定性入口 |
| IME-QUAL-002 | P1 | AUTO | L0/L2 | PARTIAL | 同语料统计目标是否进入 Top5 | 输出总体及类别 Top5 Recall |
| IME-QUAL-003 | P2 | AUTO+MANUAL | L3 | PLANNED | 同机同语料对标微软拼音/搜狗/微信 | 输出 Top1/Top5/平均选词次数，注明版本与设置 |
| IME-QUAL-004 | P2 | AUTO | L2 | PARTIAL | 个性化学习前后、重启及错误学习恢复 | 收益可复现；错误学习可撤销/衰减，不污染其他用户/上下文 |

## 15. 快捷键与边界

| ID | P | Mode | Layer | Current | 场景 | 预期 |
|---|---|---|---|---|---|---|
| IME-EDGE-001 | P1 | AUTO+MANUAL | L2/L3 | PARTIAL | composition 与已提交文本混合时 Ctrl+C/V/X/Z | 宿主快捷键与 IME 状态不冲突 |
| IME-EDGE-002 | P1 | AUTO+MANUAL | L2/L3 | PARTIAL | CapsLock/Shift、临时英文和模式切换 | 行为符合产品定义，无模式错乱 |
| IME-EDGE-003 | P1 | AUTO+MANUAL | L2/L3 | PARTIAL | 超长 composition 超过产品上限 | 合理截断/分页，无越界、崩溃或无限增长 |
| IME-EDGE-004 | P1 | AUTO | L3/L4 | PLANNED | 随机按键+焦点+IME 切换+窗口销毁 Fuzz 1h | 无 crash/hang；seed 可 100% 重放 |
| IME-EDGE-005 | P2 | AUTO+MANUAL | L3 | PARTIAL | 粘贴大文本后立即继续中文输入 | 不删除/覆盖粘贴文本，不复活旧 composition |
| IME-EDGE-006 | P1 | AUTO+MANUAL | L0/L1/L3 | PARTIAL | 逐项输入 `fh/fuhao/fuh/fuhc` | 候选 2 为 `Ω符号`；按 2 清除组合后打开 `yesymbol.exe`，不提交入口文字；L0/L1 已通过，真实 TSF 待验收 |
| IME-EDGE-007 | P1 | AUTO+MANUAL | L0/L1/L3 | PARTIAL | 逐项输入 `bq/biaoqing/biaoq/bnqk/bnq` | 候选 2 为 `😜表情`；按 2 清除组合后打开 `yesymbol.exe`；L0/L1 已通过，真实 TSF 待验收 |
| IME-EDGE-008 | P1 | AUTO+MANUAL | L0/L1/L3 | PARTIAL | 逐项输入 `shizhi/uevi/sz/shiz/uev` | 候选 2 为 `⚙️设置`；按 2 清除组合后打开设置程序；L0/L1 已通过，真实 TSF 待验收 |
| IME-EDGE-009 | P0 | AUTO+MANUAL | L1/L3 | PARTIAL | 宿主拒绝/延迟最终 cancel edit 时选择功能候选 | 清除失败则恢复原始编码且不启动程序；异步清除成功后才启动；L1 已通过，真实拒绝型宿主待验收 |

## 16. 回归集合

| 集合 | 必含内容 |
|---|---|
| Smoke | 相关 L0/L1、Host session、composition mirror、source regression |
| PR Gate | Release build + 全 CTest + 语料 + Host process + 性能 smoke |
| Smart Punctuation Gate | 全部 `SP-*` + Notepad++/ChatGPT 真人矩阵 |
| RC | 全 P0/P1 + 大词库 + 8h + 安装升级卸载 + 候选签名 |
| Final | 正式签名、干净用户包闭环、关键 Gate 重跑、公开资产哈希 |

## 17. 失败记录最小字段

```text
run_id
case_id
build_id
git_commit
package_sha256
host/process/version
steps
expected
actual
failure_class
repro_rate
log/dump/screenshot paths
owner/status
```
