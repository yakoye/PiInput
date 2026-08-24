# PiInput 智能符号与中英文标点语义规则

> Smart Punctuation & Semantic Disambiguation Specification · v1.1  
> 状态：产品与实现规范  
> 更新日期：2026-08-23

## 修改记录

| 版本 | 修改内容 | 日期 |
|---|---|---|
| v1.0 | 初版，定义中文/ASCII 标点的混合语义 | 2026-08-23 |
| v1.1 | 结合现有 TSF/Host 实现重写；定义临时判定状态、跨宿主约束、隐私边界、实现分层、规则编号和发布用例 | 2026-08-23 |

## 1. 规范结论

PiInput 的中文模式不是“所有标点都全角化”。同一段文字必须能够自然输入：

```text
1. 第一项明天处理。
会议时间：12:23，版本是 v1.0.1。
这个值是 2/3，支持 Windows/Linux/macOS。
服务器是 192.168.1.1，端口是 8080。
BIT[31:16] 表示高 16 bit，BIT[15:0] 表示低 16 bit。
```

必须同时满足以下产品契约：

1. 数值、序号、版本、IP、域名、路径和技术 token 内部使用 ASCII 符号。
2. 普通中文句子继续使用 `，。！？：；“”（）` 等中文标点。
3. 不允许仅凭“前一个按键是数字”决定标点；直通键是否进入 `OnKeyDown` 取决于宿主，Notepad++ 等应用不会为未吃掉的键保证该回调。
4. 不允许只看左侧一个字符就把数字后的句号永久判成 `.`；否则 `版本是 v1.0.1。` 无法成立。
5. 无法立即消歧时，使用可撤销的临时标点 composition；不得先提交再修改已经确认的远端文本。
6. 敏感输入范围完全旁路 Smart Punctuation，不读取上下文、不进入 Host、不记录文本。

## 2. 状态与证据口径

文档中的状态使用下列含义：

| 状态 | 含义 |
|---|---|
| 已实现 | 当前源码中存在，并有单元/组件级测试证据 |
| 部分实现 | 已有基础设施，但尚未覆盖本规范的完整语义或真实宿主 |
| 待实现 | 当前源码没有可交付实现 |
| 待运行验收 | 自动化或源码门禁不能替代的真实 Windows 应用验证 |

截至 2026-08-23 的仓库基线：

| 能力 | 状态 | 当前证据与限制 |
|---|---|---|
| 中/英/程序员标点模式 | 已实现 | `PunctuationTransformer` 和设置页已有三种模式 |
| 基础中文标点、单双引号、括号样式 | 已实现 | 核心表驱动测试和 Host session 测试覆盖 |
| Shim 向 Host 传递普通/字面标点 | 已实现 | `HostKeyKind::punctuation` / `literal_punctuation` |
| 数字后句号 | 已实现，跨宿主待验收 | 真实左右文本决策和临时单符号 composition 已落地；Notepad++ 新 DLL 已通过 `1.` 与 `v1.0.1。` |
| 时间冒号、分数/日期斜杠 | 已实现核心子集 | `/` 固定保留 ASCII；数字两侧的 `:` 保留 ASCII；`12:23`、`2/3` 与中文冒号/顿号在 Notepad++ 新 DLL 通过；时间范围和扩展 token 语义仍待补充 |
| 临时标点 composition 与右侧前瞻 | 已实现 | 后续数字保留 ASCII，否则解析为中文；焦点/异步完成按 context 与 session 校验 |
| 决策 reason code 与结构化日志 | 已实现 | `smart_context_source`、`smart_context`、`smart_punctuation`、`smart_resolution` 已进入隔离 trace |
| 多真实宿主语义矩阵 | 部分完成 | Notepad++ 已完成；ChatGPT、浏览器、Windows 搜索、Office、VS Code 仍需加载同一新 Shim 验收；Windows 搜索另要求 HKLM COM 可见 |

已实现能力可以按组件和 Notepad++ 证据写为通过，但在跨宿主矩阵完成前不得把整个 Smart Punctuation Gate 写成通过。

### 2.1 规范与当前实现的逐项差距

本文件同时承担“目标语义规范”和“当前实现校准”。规则表中的 `SP-*` 是稳定的规范 Rule ID；当前源码日志仍使用 `PUNC-*` reason code，两者在完全收敛前必须显式映射，不能把规范表当成已实现清单。

| 规则族 | 当前实现 | 当前 reason code/证据 | 尚缺 |
|---|---|---|---|
| `/` 与 `、` | 已实现 | `PUNC-SLASH-ASCII`；物理 `\` 继续由基础映射输出 `、`；Notepad++ 已验收 | 其他宿主同构建矩阵 |
| 句号：行首数字序号 | 已实现核心 | `PUNC-DECIMAL-LIST` | `1)`、`(1)`、中文序号等更完整序号分类 |
| 句号：数字中缀/数字后歧义 | 已实现 L0/核心运行时 | `PUNC-DOT-DECIMAL/VERSION/IPV4`、`PUNC-NUMERIC-PENDING`、`PUNC-PENDING-DIGIT/CHINESE` | 新细分 reason code 的 TSF/真实宿主整句验收 |
| 冒号：数字中缀/数字后歧义 | 已实现 L0/核心运行时 | `PUNC-COLON-TIME/RATIO`、`PUNC-TECHNICAL-INFIX`、`PUNC-URL/PATH`；Notepad++ `12:23` 已验收 | 连续 URL/config token 和跨宿主验收 |
| 逗号：千位/代码/中文 | 已实现核心 | `PUNC-COMMA-THOUSANDS`、`PUNC-COMMA-GROUP-PENDING`、`PUNC-PENDING-GROUP-*`、`PUNC-TECHNICAL-INFIX`、`PUNC-NUMERIC-INVALID` | 当前候选的多字符 provisional 真实宿主整句 |
| 中文正文 `，。：！？` | 已实现核心 | `PUNC-CHINESE` 或 provisional 的中文解析；URL `?`/`!` 单独保护；退出 URL/Email/Path/Code 后的中文 `。？！` L0 反例已通过 | 分号等剩余符号的 token 正反例和跨宿主验证 |
| URL/Email/Path/File/Code | L0 分类与 TSF 路由已扩展 | `PUNC-URL/EMAIL/PATH/FILENAME/TECHNICAL-INFIX/BOUNDARY/QUOTE/PREFIX`；四类 token 退出后立即恢复中文的 L0 已通过，Controlled TSF Oracle 已编译 | 当前候选真实 profile 执行 Oracle，确认不引入跨键状态 |
| 连字符、百分号、单位 | 基础本义已满足 | `-`、`%` 和 `/` 在中文模式本来就是 ASCII，已有表驱动映射；Shift+- 保留显式中文破折号；`折扣80%，` 连续物理键 Oracle 已编译 | 当前候选实跑和更多工程单位整句/跨宿主验收，不增加无必要的猜测状态 |
| 技术括号/引号/问号/感叹号 | 核心上下文已实现 | `BIT[31:16]`、`func(x)`、`file_name`、赋值引号、`!flag`、URL `?`/`!` 与中文反例已有 L0；Controller Oracle 已写 | 当前候选真实 profile 实跑及更多 JSON/shell/Markdown 边界 |
| provisional lifecycle | 运行时路径已实现，直接用例不完整 | 单符号 composition、数字/非数字解析、Backspace/Esc、session/context 校验和有序 replay | Controlled TSF Host 下开始/更新/提交/取消/销毁的直接断言 |
| 真实宿主 | 部分完成 | Notepad++ 新 DLL 核心矩阵通过 | Notepad、ChatGPT、Chromium、Office、VS Code 同一 build identity |

因此“智能标点核心子集完成”与“05 完整完成”是两个不同结论；当前后者仍为否。

## 3. 不可回避的歧义

用户在数字后按 `.` 时，仅凭当下左侧文本无法知道其最终意图：

- `1. 第一项`：序号点，应为 ASCII。
- `3.14`：小数点，应为 ASCII。
- `v1.0.1.2`：版本内部点，应为 ASCII。
- `版本是 v1.0.1。`：句末标点，应为中文句号。

冒号也存在同样问题：

- `12:23`、`16:9`：ASCII 冒号。
- `共有 12 项：`：中文冒号。

因此以下算法被明确禁止：

```text
if previous_character_is_digit:
    output_ascii_symbol()
```

它只能解决数值内部符号，却会永久消灭数字后的中文句号和中文冒号。

## 4. 上下文模型

### 4.1 Context 类型

| Context | 典型内容 | 默认策略 |
|---|---|---|
| `SENSITIVE` | 密码、PIN、private scope | 完全旁路 PiInput |
| `COMPOSITION` | 正在输入拼音或英文候选 | 先按 composition 规则处理，不读取远端未提交猜测 |
| `NUMERIC` | `3.14`、`12:23`、`2/3`、`1,000` | ASCII 内部符号 |
| `SEQUENCE` | `1.`、`1)`、`(1)`、`一、` | 保留选定序号格式 |
| `ASCII_TOKEN` | `v1.0.1`、`README.md`、`key:value` | ASCII 内部符号 |
| `TECHNICAL` | `BIT[31:16]`、`0xFFFF`、`GT/s` | ASCII 内部符号 |
| `URL_EMAIL_PATH` | URL、Email、Windows/POSIX 路径 | token 内 ASCII |
| `CHINESE_TEXT` | 中文正文、描述、段落 | 中文标点 |
| `AMBIGUOUS` | 当下缺少右侧字符，或两种语义都成立 | 进入临时判定或使用明确物理键 |

### 4.2 上下文来源

决策输入必须来自当前编辑器的真实选择位置，而不是上一按键回调遗留的布尔值。默认来源是当前 `ITfContext`；若宿主的 TSF 文档存储只暴露活动 composition、对已经直通落盘的字符返回空窗口，则必须使用该宿主公开的只读文本接口补全同一快照：

- 标准 TSF 宿主：同步只读 edit session 中的 `ITfRange`；
- Scintilla/Notepad++：当前进程内、类名严格为 `Scintilla` 的只读消息接口；
- 其他宿主：没有可信文本来源时保持 `AMBIGUOUS`，不得退回“上一按键是数字”的状态猜测。

快照内容包括：

- 选择点左侧和右侧的有限窗口；
- 当前段落/行边界；
- 当前未提交 composition；
- 当前输入模式和标点模式；
- 当前按键、Shift 状态及物理 OEM 键；
- 已识别 token 类型；
- 用户是否正在编辑已有文本。

宿主适配器只允许读取当前进程、当前焦点编辑控件的有限窗口，不允许写文档，不允许跨进程读取，也不允许在敏感 scope 中调用。快照日志只记录 `tsf/scintilla/unavailable`、长度和分类，不记录原文。

窗口只需覆盖局部 token，不得扫描整篇文档。建议上限为左右各 64 个 UTF-16 code unit，并在日志中只记录分类结果和长度；默认不记录原文。

## 5. 决策架构

实现必须分成三层，不能把语义判断散落在 TSF 按键回调中：

```text
TSF Context Snapshot
        |
        v
SmartPunctuationEngine (纯逻辑、无 COM、可单元测试)
        |
        +-- PASS_THROUGH
        +-- COMMIT_ASCII
        +-- COMMIT_CHINESE
        +-- BEGIN_PROVISIONAL
        +-- UPDATE_PROVISIONAL
        +-- COMMIT_PROVISIONAL
        +-- CANCEL_PROVISIONAL
```

### 5.1 TSF 适配层

- 在同步只读 edit session 内读取真实选择和局部上下文。
- 若 TSF 返回成功但上下文窗口为空，允许调用经过进程、窗口类和只读操作三重约束的宿主快照适配器；Notepad++ 使用 Scintilla 当前位置与只读字符查询。
- 不在 `OnTestKeyDown` 中写文档或改变语义状态。
- `OnTestKeyDown` 与 `OnKeyDown` 必须对同一快照得到一致的“是否吃键”结果。
- 不依赖未吃掉按键之后仍会收到 `OnKeyDown`。
- 所有替换仅作用于 PiInput 自己拥有的临时 composition range。

### 5.2 纯决策层

- 输入是不可变的 `PunctuationContext`。
- 输出是 `PunctuationDecision`，包含 action、输出、`rule_id` 和置信类型。
- 不访问 COM、窗口、Host、文件、注册表或全局按键状态。
- 相同输入必须产生相同输出。

### 5.3 映射层

现有 `PunctuationTransformer` 继续负责“按键到符号”的确定性映射，例如中文模式下 `.` 对应 `。`；它不负责 token 识别和前瞻。

## 6. 临时标点状态

### 6.1 使用条件

当光标位于文本末端、当前符号需要右侧字符才能可靠判定时，进入 `ProvisionalPunctuation`：

```text
Idle
  -> BeginProvisional(ascii_preview, source_key, context_snapshot)
  -> ResolveByNextInput / ResolveByBoundary / Cancel
  -> Idle
```

临时标点必须是 PiInput 自己拥有的、最多一个符号的 TSF composition。显示内容可先使用用户物理键对应的 ASCII 字形，让 `1.`、`12:` 的输入保持即时反馈；在最终提交前可以安全替换。

### 6.2 下一输入的处理

| 下一输入 | 处理 |
|---|---|
| 数字且满足对应数值语法 | 提交 ASCII 符号，并保证数字按原顺序进入宿主 |
| 中文字母输入/中文正文 | 按 token/序号规则决定中文或 ASCII 后，再处理当前键 |
| Backspace | 普通 provisional 取消临时符号；千位分组 provisional 已积累数字时先逐位回退，不删除符号左侧已提交字符 |
| Esc | 取消临时符号并回到 Idle；千位分组已积累数字必须保留，不能随符号一起吞掉 |
| 光标移动/鼠标改变选择 | 先按边界规则定稿，再移动；失败时宁可保留预览，不改写其他文本 |
| 焦点丢失/Context 销毁 | 在原 context 内定稿或取消；不得把旧 range 带到新 context |
| 输入法切换/敏感 scope | 立即结束或取消，清除全部临时状态 |

### 6.3 顺序保证

解析临时符号和下一按键必须作为一个有序事务。不能先异步提交标点、再让下一数字直接穿过，否则快速输入可能变成 `121:3`、丢键或重复提交。

## 7. 句号 `.` / `。`

### 7.1 规则

| Rule ID | 场景 | 输出 |
|---|---|---|
| `SP-DOT-NUMERIC` | 两侧为数字，且 token 满足小数/版本/IP/规格 | `.` |
| `SP-DOT-SEQUENCE` | 行首或列表边界后的整数序号 | `.` |
| `SP-DOT-ASCII-TOKEN` | 域名、文件名、扩展名、英文技术 token 内 | `.` |
| `SP-DOT-CN-SENTENCE` | 中文正文句末 | `。` |
| `SP-DOT-PROVISIONAL` | 左侧为数字但右侧尚不存在 | 临时 `.`，等待后续输入或边界 |

### 7.2 必须成立的示例

```text
1. 第一项
3.14
v1.0.1
192.168.1.1
README.md
PCIe 6.0
版本是 v1.0.1。
共有 12 项。
```

`1. 第一项` 的序号规则优先于“下一输入是中文”的句末推断。

## 8. 冒号 `:` / `：`

| Rule ID | 场景 | 输出 |
|---|---|---|
| `SP-COLON-TIME` | `HH:MM`、`HH:MM:SS` 的合法位置 | `:` |
| `SP-COLON-RATIO` | 数字比例，如 `16:9` | `:` |
| `SP-COLON-TECH` | 位域、配置、协议、URL scheme 内 | `:` |
| `SP-COLON-CN-LEAD` | 中文说明、提示或引出内容 | `：` |
| `SP-COLON-PROVISIONAL` | 左侧为数字且右侧尚不存在 | 临时 `:`，等待右侧数字或边界 |

示例：

```text
会议时间：12:23。
视频比例是 16:9。
BIT[31:16]
key:value
共有 12 项：
```

时间规则应验证范围：小时 `0..23`、分钟/秒 `0..59`。超出范围时仍可按比例或技术 token 处理，不能简单拒绝 ASCII 冒号。

## 9. 斜杠 `/` / 顿号 `、`

这是天然歧义，不能通过“左右都是中文”可靠判断。v1.1 采用明确物理键策略：

| 物理键 | 中文模式默认输出 | 用途 |
|---|---|---|
| `/` 键（`VK_OEM_2`，无 Shift） | `/` | 分数、日期、路径、任选、并列、单位 |
| `\` 键（`VK_OEM_5`，无 Shift） | `、` | 中文列举顿号 |

这使下列文本不需要猜测或切换输入法：

```text
2/3
2026/08/23
Windows/Linux/macOS
读/写
开/关
白色/黑色
白色、黑色
苹果、香蕉、橘子
```

Shift 组合仍按键盘本义处理：`Shift+/` 为问号，`Shift+\` 为竖线，并继续受中文/技术 context 规则约束。

## 10. 逗号 `,` / `，`

| Rule ID | 场景 | 输出 |
|---|---|---|
| `SP-COMMA-THOUSANDS` | 合法千位分组 | `,` |
| `SP-COMMA-ASCII` | CSV、代码、ASCII token | `,` |
| `SP-COMMA-CN` | 中文正文停顿 | `，` |

千位分隔必须验证数字组结构，不能把任意“数字后逗号”都当作 ASCII：

```text
价格是 1,299.50 元，今天便宜。
```

## 11. 其他高频符号

### 11.1 连字符与负号

以下场景保留 ASCII `-`：日期、负数、范围、版本名、命令行参数、英文复合 token。

```text
2026-08-23  -20  1-10  PiInput-v0.8.0  --help
```

不得把两个 `-` 自动替换成中文破折号，除非未来有独立、可关闭且不会影响代码/命令行的规则。

### 11.2 百分号与单位

数字后的 `%` 和单位 token 内的 `/` 保持 ASCII：

```text
80%  99.5%  64GB  5GHz  10ms  32GT/s  100MB/s  10km/h
```

### 11.3 URL、Email、路径和文件名

可靠识别后，token 内保护以下字符：

```text
: / . ? = & # % @ _ - +
```

离开 token 后必须立即恢复中文语义，不能把整段后续文本锁在 ASCII 标点状态。

### 11.4 括号、引号、问号和感叹号

- 中文正文：`（说明）`、`“你好”`、`你去吗？`、`很好！`
- 技术上下文：`func(a)`、`array[3]`、`{x}`、`"hello"`、`!flag`
- URL 中的 `?` 必须受 URL token 保护。
- 用户已经显式修正并提交的引号/括号不得被后台二次改写。

## 12. 规则优先级

决策顺序固定如下：

1. `SENSITIVE`：完全旁路。
2. 已存在的 PiInput composition：按 composition 语义处理。
3. 用户明确选择的标点模式：English/Programmer 模式优先 ASCII。
4. URL、Email、Path、Code、Technical token：ASCII。
5. 确定性 Numeric 规则：ASCII。
6. Sequence 规则：保留序号格式。
7. 普通中文正文且规则明确：中文标点。
8. 缺少右侧信息：进入临时判定。
9. 天然歧义：使用明确物理键或保守输出，不做后台猜测。

任何规则都不得覆盖第 1 条隐私边界，也不得改写不属于当前 PiInput composition 的已提交文本。

## 13. Reason Code 与日志

每次决策至少输出以下非文本字段；当前实现已经输出部分 `PUNC-*` reason code，统一事件字段仍是目标 schema：

```text
event_seq
context_id_hash
source_key
shifted
input_mode
punctuation_mode
left_token_type
right_token_type
decision
rule_id
provisional_state
elapsed_us
```

默认日志不得记录密码范围、原始上下文、完整 URL、Email、剪贴板内容或用户正文。诊断构建如需文本片段，必须显式启用、限长并写入进程隔离日志。

示例：

```text
KEY=. LEFT=INTEGER RIGHT=PENDING CONTEXT=AMBIGUOUS RULE=PUNC-NUMERIC-PENDING DECISION=BEGIN_PROVISIONAL
KEY=0 LEFT=DOT_PROVISIONAL RIGHT=DIGIT CONTEXT=NUMERIC RULE=PUNC-PENDING-DIGIT DECISION=COMMIT_ASCII
KEY=a LEFT=DOT_PROVISIONAL RIGHT=PROSE CONTEXT=CHINESE_TEXT RULE=PUNC-PENDING-CHINESE DECISION=COMMIT_CHINESE
```

## 14. P0 自动化用例

| Case ID | 目标文本/操作 | 关键断言 |
|---|---|---|
| `SP-MIX-001` | `1.明天干嘛。` | 同句出现序号 ASCII `.` 与中文 `。` |
| `SP-MIX-002` | `这个是2/3，那么我们选白色/黑色，白色、黑色。` | 分数/选项 `/`、列举 `、` 与中文标点共存 |
| `SP-MIX-003` | `会议时间：12:23，版本是v1.2.3。` | 中文冒号、时间冒号、版本点和句号均正确 |
| `SP-MIX-004` | `价格是1,299.50元，折扣为80%。` | 千位、小数、百分号与中文标点共存 |
| `SP-MIX-005` | `服务器是192.168.1.1，端口是8080。` | IPv4 内部点 ASCII，句末中文 |
| `SP-MIX-006` | `BIT[31:16]表示高16bit，BIT[15:0]表示低16bit。` | 技术方括号/冒号不中文化 |
| `SP-MIX-007` | `支持Windows/Linux/macOS，选择是/否即可。` | 中文句子中的 `/` 保持本义 |
| `SP-MIX-008` | `访问https://example.com?a=1&b=2，然后继续输入中文。` | URL 全 ASCII，退出 token 后恢复中文 |
| `SP-MIX-009` | `文件是PiInput-v0.8.0.zip，请打开。` | 文件名/版本符号保护，随后恢复中文 |
| `SP-STATE-001` | 数字、句点、数字快速连打 | 顺序正确，Lost/Duplicate=0 |
| `SP-STATE-002` | 临时符号后 Backspace/Esc | 只取消临时符号，不伤及左侧文本 |
| `SP-STATE-003` | 临时符号时 Alt+Tab/切输入框 | 无幽灵 composition，无跨 context 回写 |
| `SP-STATE-004` | 在已有文本中间插入数值符号 | 使用真实左右文本立即判定 |
| `SP-PRIV-001` | Password/PIN/private scope | 完全旁路，无 Host/日志文本 |
| `SP-HOST-001` | 同一序列在 Notepad 与 Notepad++ | 输出一致，不依赖直通键 `OnKeyDown` |
| `SP-HOST-002` | 同一序列在 ChatGPT/Chromium/Electron | 输出一致，无重复提交 |

每条规则必须至少有一个正例、一个不得误转换的反例和一个快速输入顺序用例。

## 15. 发布门禁

Smart Punctuation 只有同时满足以下条件才可标记为完成：

- 纯决策层全部规则单元测试通过。
- TSF 临时 composition 的开始、更新、提交、取消和 context 销毁测试通过。
- `SP-MIX-*`、`SP-STATE-*`、`SP-PRIV-*` 全部通过。
- Notepad、Notepad++、Word、Chrome/Edge、VS Code、ChatGPT Windows App 的 P0 场景人工或受控自动化通过。
- Lost Key、Duplicate Commit、Unexpected Commit、Wrong Commit 均为 0。
- 快速输入不会造成标点与下一数字乱序。
- 源码中不存在依赖直通键 `OnKeyDown` 更新“上一键是数字”的状态变量。
- 诊断日志能给出稳定 `rule_id`，且敏感范围无文本泄漏。

## 16. 实施状态与后续顺序

截至 2026-08-23 已完成：

1. 已替换“上一键是数字”的回调状态补丁，决策改用当前文档快照。
2. 已落地纯 `SmartPunctuationEngine`、严格数值与 URL/Email/Path/File/技术中缀及边界表驱动测试、TSF/Scintilla 局部快照、单符号 provisional composition、有序下一键处理、`/`/`、` 物理键策略、URL `?`/`!` 路由和隐私安全 reason code；`BIT[31:16]`、`func(x)`、`file_name` 与中文括号/叹号反例均有 L0 覆盖。
3. 千位分组已使用三位 provisional：前两位继续等待，第三位才提交 ASCII；非数字边界转中文且保留数字，Backspace 逐位回退，Esc 只撤销逗号。
4. Controlled TSF Host、物理扫描码 Controller、焦点/context Oracle 与真实 PiInput profile/DLL 身份 smoke 已实现；场景覆盖三位千分位、非完整分组、Backspace/Esc、跨输入框、context 销毁、技术符号及 Password/PIN，并可持续循环供 TSF/App soak 采样。非 PiInput 夹具模式已通过；包闭环已接入 `--expected-tsf`，会拒绝加载旧 DLL 或错误注册路径，当前候选模式待安装后执行。
5. Notepad++ 新 DLL 已实机通过 `1.`、`12:23`、`2/3`、`v1.0.1。`、中文冒号和反斜杠顿号核心矩阵。
6. 数字 provisional 已收窄到 `.`、`:`、`,`；数字后的 `？/！` 不再被误判为数值符号。命令前缀 `!flag` 只有存在右侧 ASCII token 时保留 ASCII，行首独立 `！` 仍可直接获得。技术括号还必须看到英文字母、下划线或技术分隔符，`BIT[31:16]`/`func(x)` 保持 ASCII，而 `第1（测试）` 不再被单个数字误判；对应 L0 反例已通过，当前候选物理键 Oracle 已加入数字后中文问号。
7. URL、Email、Path、Code token 退出边界的 L0 正反例已通过；Controlled TSF smoke 已加入四类 token 后恢复中文标点及 `折扣80%，` 连续物理键 Oracle。控制器已重新编译，真实 PiInput profile 分支须随冻结候选执行后才能记为通过。

剩余工作按以下顺序实施：

1. 用当前候选的 Controlled TSF smoke 执行已编译的 token 退出与百分号连续输入 Oracle，确认局部 token 重建不引入跨键“上一状态”。
2. 补分号、更多引号/工程单位等剩余上下文规则并明确天然歧义的保守策略。
3. 在当前候选安装后运行 Controlled TSF 的真实 profile/lifecycle smoke，按真实结果修复而不是把“Oracle 已写”记为通过。
4. 使用同一 build identity 完成 Notepad、ChatGPT、Chromium、Office、VS Code 矩阵。
5. 全部 `SP-MIX-*`、`SP-STATE-*`、`SP-PRIV-*` 通过后，才能关闭 G4；随后再做正式签名安装包闭环。

## 17. 最终原则

智能标点的成功标准不是“自动替换得最多”，而是：

- 明确场景判断正确；
- 歧义场景不损坏用户输入；
- ASCII 原义和中文标点都能直接获得；
- 宿主回调差异不改变结果；
- 所有临时状态可取消、可追踪、不会跨 context 泄漏。

`1. 第一项`、`12:23`、`2/3`、`v1.0.1。` 和 `白色/黑色，白色、黑色。` 必须在同一个中文输入模式中自然成立。
