# PiInput 版本测试报告模板

> Release Qualification Report Template · v1.3
> 使用方式：复制本文件并命名为 `PiInput_<version>_版本测试报告_<date>.md`

## 0. 填写规则

- 只允许 `PASS`、`FAIL`、`BLOCKED`、`NOT RUN`、`N/A`。
- `BLOCKED`/`NOT RUN` 不能计入通过率，也不能推出“允许发布”。
- 每个 PASS 必须有构建身份和证据路径；不得填写“本地试过正常”。
- 脚本存在、测试注册或源码检查不能代替脚本实际执行。
- 正式版不允许使用 dirty 构建、测试证书或未运行的 8h soak。

复制为具体版本验证记录时保留并填写以下机器 Gate；正式 tag 只接受全部 `PASS`：

```text
host_soak_8h=NOT_RUN
tsf_app_soak_8h=NOT_RUN
p0_real_host_matrix=NOT_RUN
```

## 1. 基本信息

| 项目 | 填写内容 |
|---|---|
| 版本 | PiInput v____ |
| 构建号 / Build ID | ____ |
| Git Commit | ____ |
| Git Dirty | YES / NO |
| 测试类型 | Smoke / PR / Nightly / RC / Final |
| 测试开始/结束 UTC | ____ / ____ |
| Windows 版本与 build | ____ |
| CPU / 内存 | ____ |
| 键盘布局 | ____ |
| DPI / 显示器 | ____ |
| 测试负责人 | ____ |
| CI Run / 自动化报告 | ____ |
| 日志与证据根目录 | ____ |

## 2. 产物身份

| 产物 | 文件名 | Version | Build ID | SHA-256 | 签名状态 |
|---|---|---|---|---|---|
| ZIP |  |  |  |  |  |
| Installer |  |  |  |  |  |
| PiInputTSF.dll |  |  |  |  |  |
| PiInputHost.exe |  |  |  |  |  |
| PiInput-Settings.exe |  |  |  |  |  |
| PiInput-Uninstall.exe |  |  |  |  |  |

签名证书：

| 项目 | 值 |
|---|---|
| Subject |  |
| Thumbprint |  |
| Issuer |  |
| Validity |  |
| Timestamp Subject / Thumbprint |  |
| RFC3161 Timestamp URL / status |  |
| `signtool verify /pa /all` 日志 |  |
| `signatures.json` |  |

## 3. 总体结论

| 结论项 | 结果 |
|---|---|
| 总体结论 | PASS / CONDITIONAL PASS / BLOCKED / FAIL |
| P0 未关闭数 |  |
| P1 未关闭数 |  |
| P2 未关闭数 |  |
| Release Gate | PASS / BLOCKED / FAIL |
| 是否允许发布 | YES / NO |
| Release Owner 结论 |  |

结论依据（必须写明阻断项或豁免）：

```text

```

## 4. Gate 汇总

| Gate | 目标 | 结果 | 证据/备注 |
|---|---|---|---|
| G0 构建身份 | version/build ID/commit/hash 对应；dirty=NO |  |  |
| G1 自动回归 | Release 全 CTest 执行；Failed=0 |  |  |
| G2 输入完整性 | Lost/Duplicate/Wrong/Unexpected/Cross-context=0 |  |  |
| G3 隐私 | 敏感 scope 全旁路，真实输入框矩阵通过 |  |  |
| G4 智能标点 | 全 SP P0 + Notepad++/ChatGPT 通过 |  |  |
| G5 宿主兼容 | P0 宿主 100% |  |  |
| G6 性能/资源 | 指标或正式豁免；8h 两类 soak 通过 |  |  |
| G7 签名/包闭环 | 正式签名；干净用户安装升级卸载 |  |  |
| G8 发布资产 | tag/source/notes/assets/hash 对应 |  |  |

## 5. 自动化执行摘要

| 项目 | 值 |
|---|---|
| CTest Registered |  |
| CTest Executed |  |
| PASS / FAIL / SKIP / DISABLED |  |
| CTest JUnit |  |
| Unified result.json |  |
| Artifact manifest / SHA-256 |  |
| Unified gate_status | PASS / FAIL / BLOCKED / NOT_RUN / N/A |
| Executed pass rate | PASS / (PASS + FAIL)，不得包含 BLOCKED/NOT_RUN/N/A |
| 结构化语料 |  /  |
| 专业词语料 |  /  |
| Host process |  |
| Source regression |  |
| Performance smoke |  |
| Test log |  |

场景与事件规模：

| 指标 | 实际值 | RC 最小要求 | 结果/证据 |
|---|---|---|---|
| Total Cases / Executed |  | P0/P1 全执行 |  |
| Key Events |  | >=100,000 |  |
| Focus Switch |  | >=10,000 |  |
| IME Switch |  | >=10,000 |  |
| Context Create/Destroy |  | >=10,000 |  |

被跳过/禁用项目及原因：

| Test | 原因 | 是否影响 Gate |
|---|---|---|
|  |  |  |

## 6. 输入完整性

| 指标 | 事件数/场景数 | 结果 | Gate | 证据 |
|---|---|---|---|---|
| Key Events |  |  |  |  |
| Lost Key |  |  | 0 |  |
| Duplicate Commit |  |  | 0 |  |
| Unexpected Commit |  |  | 0 |  |
| Wrong Commit |  |  | 0 |  |
| Cross-context Write |  |  | 0 |  |
| Stuck Composition |  |  | 0 |  |
| Stuck Candidate |  |  | 0 |  |
| Crash / Hang |  |  | 0 |  |

## 7. 智能标点

| Case | 目标 | 结果 | 宿主/证据 |
|---|---|---|---|
| SP-MIX-001 | `1.明天干嘛。` |  |  |
| SP-MIX-002 | `这个是2/3，那么我们选白色/黑色，白色、黑色。` |  |  |
| SP-MIX-003 | `会议时间：12:23，版本是v1.2.3。` |  |  |
| SP-MIX-004 | `价格是1,299.50元，折扣为80%。` |  |  |
| SP-MIX-005 | `服务器是192.168.1.1，端口是8080。` |  |  |
| SP-MIX-006 | `BIT[31:16]表示高16bit，BIT[15:0]表示低16bit。` |  |  |
| SP-MIX-007 | `支持Windows/Linux/macOS，选择是/否即可。` |  |  |
| SP-MIX-008 | `访问https://example.com?a=1&b=2，然后继续输入中文。` |  |  |
| SP-MIX-009 | `文件是PiInput-v0.8.0.zip，请打开。` |  |  |
| SP-STATE-001 | 快速数字-符号-数字顺序 |  |  |
| SP-STATE-002 | Backspace/Esc 临时状态 |  |  |
| SP-STATE-003 | focus/context 销毁 |  |  |
| SP-PRIV-001 | 敏感范围完全旁路 |  |  |

宿主一致性：

| 序列 | Notepad | Notepad++ | ChatGPT | Chrome/Edge | VS Code |
|---|---|---|---|---|---|
| `1. 第一项。` |  |  |  |  |  |
| `1.文本` 不被回写，`1..` 得到 `1.。` |  |  |  |  |  |
| `12:23。` |  |  |  |  |  |
| `2/3。` |  |  |  |  |  |

## 8. 敏感输入

| 场景 | Host request | Candidate/Composition | 日志文本 | 宿主可输入 | 结果 |
|---|---|---|---|---|---|
| Win32 Password |  |  |  |  |  |
| Browser Password |  |  |  |  |  |
| PIN/Numeric password |  |  |  |  |  |
| WinUI/Windows App |  |  |  |  |  |
| 普通->敏感 context 复用 |  |  |  |  |  |
| 敏感->普通恢复 |  |  |  |  |  |

## 9. 兼容性结果

| 宿主 | 基础输入 | 标点 | 焦点/context | IME 切换 | 粘贴后输入 | 候选 UI | 结果 | 证据 |
|---|---|---|---|---|---|---|---|---|
| Notepad |  |  |  |  |  |  |  |  |
| Controlled TSF Host |  |  |  |  |  |  |  | 物理扫描码结果、实际加载 DLL 路径、Password/PIN 旁路 JSON |
| Notepad++ |  |  |  |  |  |  |  |  |
| Chrome |  |  |  |  |  |  |  |  |
| Edge |  |  |  |  |  |  |  |  |
| VS Code |  |  |  |  |  |  |  |  |
| ChatGPT Windows App |  |  |  |  |  |  |  |  |
| Word |  |  |  |  |  |  |  |  |
| Excel |  |  |  |  |  |  |  |  |
| Windows Terminal |  |  |  |  |  |  |  |  |
| PowerShell/CMD |  |  |  |  |  |  |  |  |
| Windows App/WinUI |  |  |  |  |  |  |  |  |
| WPF |  |  |  |  |  |  |  |  |
| Qt |  |  |  |  |  |  |  |  |

## 10. 性能

### 10.1 Engine/Host

| 指标 | Count | P50 | P90 | P95 | P99 | MAX | Baseline | 结论 |
|---|---|---|---|---|---|---|---|---|
| Engine query |  |  |  |  |  |  |  |  |
| Host request |  |  |  |  |  |  |  |  |
| Cold start |  |  |  |  |  |  |  |  |

### 10.2 UI End-to-End

| 指标 | Host | Count | P50 | P90 | P95 | P99 | MAX | 结论 |
|---|---|---|---|---|---|---|---|---|
| Key -> Composition |  |  |  |  |  |  |  |  |
| Key -> Candidate |  |  |  |  |  |  |  |  |
| Commit visible |  |  |  |  |  |  |  |  |
| Smart punctuation resolve |  |  |  |  |  |  |  |  |
| Candidate page |  |  |  |  |  |  |  |  |

不得把 Host-only 指标填入 UI End-to-End 表。

## 11. 词库与内存映射

| 项目 | 结果 | 证据 |
|---|---|---|
| Lexicon entry count |  |  |
| `lexicon_storage` | mmap / heap / unknown |  |
| mapped bytes |  |  |
| 首次加载时间 |  |  |
| Host baseline private/working set |  |  |
| 查询一致性 |  |  |

## 12. 中文输入质量

| 语料/指标 | PiInput 本版 | PiInput 基线版 | 微软拼音 | 搜狗 | 微信输入法 | 证据/备注 |
|---|---|---|---|---|---|---|
| 语料版本与样本数 |  |  |  |  |  |  |
| Top1 Accuracy |  |  |  |  |  |  |
| Top5 Recall |  |  |  |  |  |  |
| 平均选词次数 |  |  |  |  |  |  |
| 长句一次上屏率 |  |  |  |  |  |  |
| 个性化学习收益/污染回归 |  |  | N/A | N/A | N/A |  |

如果没有同机同语料竞品结果，相应列填写 `NOT RUN`，不能空白，也不能把 313 条结构化语料的确定性通过率解释为真实语言准确率。

## 13. Soak 与资源曲线

### 13.1 Host-only

| 项目 | 值 |
|---|---|
| 实际时长 |  |
| Sample interval / count |  |
| Workload iterations / failures |  |
| Private start/end/peak/slope |  |
| Working Set start/end/peak |  |
| Handle start/end/peak/slope |  |
| Thread start/end/peak |  |
| Crash/Hang |  |
| `summary.json` / CSV |  |
| 结果 | PASS / FAIL / NOT RUN |

完整资源表（Host-only 与 TSF/App 分别填写，宿主不适用项写 `N/A`）：

| Process/指标 | Start | End | Peak | Delta | Slope/hour | 结果 |
|---|---|---|---|---|---|---|
| Host Private Bytes |  |  |  |  |  |  |
| Host Working Set |  |  |  |  |  |  |
| Host Handle Count |  |  |  |  |  |  |
| Host Thread Count |  |  |  |  |  |  |
| Host CPU |  |  |  |  |  |  |
| Shim host Private/Working Set |  |  |  |  |  |  |
| Shim host Handle/Thread |  |  |  |  |  |  |
| Shim host GDI Objects |  |  |  |  |  |  |
| Shim host USER Objects |  |  |  |  |  |  |
| Idle CPU (5 min) |  |  |  |  |  |  |

### 13.2 TSF/真实宿主

| 项目 | 值 |
|---|---|
| 宿主与 workload |  |
| 实际时长 |  |
| Key/Focus/Context/IME event count |  |
| Controller iterations / context recreates |  /  |
| Minimum iterations/hour / density gate |  / PASS・FAIL |
| Lost/Duplicate/Stuck |  |
| Host + 宿主资源趋势 |  |
| Controller status / CSV / `summary.json` |  |
| 实际加载 PiInputTSF.dll |  |
| 结果 | PASS / FAIL / NOT RUN |

短时 smoke 的时长必须如实填写，不能放入“8h PASS”。

## 14. 包闭环

| 检查 | 结果 | 证据 |
|---|---|---|
| ZIP SHA-256 |  |  |
| 唯一包根/必需 payload |  |  |
| 禁止源码/脚本泄漏 |  |  |
| 包内 version/build ID |  |  |
| 全 PE Authenticode + RFC3161 timestamp |  |  |
| 干净用户静默安装 |  |  |
| 二次覆盖安装 |  |  |
| 前版本升级 |  |  |
| 静默卸载 |  |  |
| 重启后 profile/残留检查 |  |  |
| 注册 TSF/Host 固定路径 |  |  |
| HKCU/HKLM `InprocServer32` 均指向安装后同一 TSF DLL |  |  |
| SearchHost PID、实际加载 PiInputTSF.dll 路径及中文最终文本 |  |  |
| Windows 搜索首字母系统候选行可见；`BeginUIElement` 由宿主接管，外部 popup 被抑制 |  |  |
| 普通桌面宿主外部候选窗可见，`GW_OWNER` 为文本宿主顶层窗口 |  |  |
| 包内/安装后 Host 与 TSF 哈希对应 |  |  |
| Controlled TSF 实际加载 DLL + 物理键 smoke |  |  |
| 公开 Release ZIP 回下载与本地/sidecar 哈希 |  |  |
| 内置计算器/程序员计算器/画图别名与候选位置 |  |  |
| `kuaijie` 撤销别名不产生动作候选 |  |  |
| 快捷表五字段、0～64 行、增删改、恢复默认和旧三槽迁移 |  |  |
| 英文候选开启时动作位置；关闭时逐键直输不拦截 |  |  |
| Windows 工具模板搜索/分类/导入、Everything 缺失与参数化启动 |  |  |
| YeTool 模板 MIT 许可随包且不分发 Everything 本体 |  |  |
| `bin` 中 RegCalc HTML/CSS/JS 三项运行时资产完整 |  |  |
| 同一已安装候选实际打开系统工具/HTML/自定义目标 |  |  |

## 15. 未关闭问题

| Bug ID | P | 现象 | 影响范围 | Workaround | Gate | 是否阻断 |
|---|---|---|---|---|---|---|
|  |  |  |  |  |  |  |

## 16. 失败用例

| Case ID | Failure Class | 次数/复现率 | Expected | Actual | 日志/Dump | Owner/状态 |
|---|---|---|---|---|---|---|
|  |  |  |  |  |  |  |

## 17. 豁免

硬 Gate（隐私、输入完整性、P0/P1、正式签名）不可豁免。PI-TARGET/P2 豁免填写：

| 项目 | 当前值 | 目标 | 用户影响 | 计划 | 批准人/日期 |
|---|---|---|---|---|---|
|  |  |  |  |  |  |

## 18. 发布签字

| 角色 | 结论 | 姓名/日期 |
|---|---|---|
| 开发 | 同意 / 不同意 |  |
| 测试 | 同意 / 不同意 |  |
| Release Owner | 同意 / 不同意 |  |

最终发布结论：

```text

```
