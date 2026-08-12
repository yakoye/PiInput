# PiInput 功能入口、中文帮助与符号中心实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在现有稳定 TSF Shim + 可升级 Host 架构中加入不写入正文的功能菜单、全中文帮助、140 个内置符号分类浏览、用户自定义符号与最近使用，并保证 Markdown 反引号默认完全兼容。

**Architecture:** 业务状态全部位于 `PiInputHost.exe`。新增独立的 `HelpCatalog`、增强后的 `SymbolIndex`、`RecentSymbols` 与 `FunctionMenu`；`HostSession` 只协调普通组合和功能菜单。Host 协议升级到 v2，把正文组合 `raw` 与候选窗口的 `prompt/visible` 分离，并把命令快捷键策略同步给稳定 Shim。Shim 只识别当前缓存策略对应的按键并执行 Host 回复，不读取帮助或符号数据。

**Tech Stack:** C++20、CMake、Windows TSF/COM、现有有界 Named Pipe 协议、UTF-8 TSV、CTest、PowerShell 发布脚本。

## Global Constraints

- 不增加 AI、云联想、语音或网络热路径。
- 帮助和符号中心只使用现有候选窗口，不创建独立窗口、网页或 WebView。
- 默认快捷键是 `Ctrl+Alt+反引号`；`Ctrl+反引号` 默认交给应用。
- `middle_dot_alias=false` 时单、双、三反引号在中文和英文状态均原样输出。
- 功能菜单与正文组合必须分离；菜单标题不得进入 TSF Composition。
- 候选窗口默认只显示一行，只有用户按 `=`/下方向键后才展开。
- 内置符号只读；用户符号、最近使用和设置位于 `%LOCALAPPDATA%\PiInput\UserData`，升级和默认卸载保留。
- 单条坏数据只能影响该条记录；帮助或符号错误不得导致中文输入引擎无法启动。
- 已显示候选使用不可变 generation；过期候选 ID 必须拒绝。
- 本阶段保留拆字路由标识，但不显示或实现尚未完成的拆字功能。
- 不自动安装，不推送远程仓库；完成后只生成 Windows ZIP 给用户自行安装。

---

## 文件结构

### 新建

- `include/piinput/help_catalog.h`、`src/help_catalog.cpp`：中文帮助解析和层级查询。
- `include/piinput/recent_symbols.h`、`src/recent_symbols.cpp`：最近符号记录和原子保存。
- `include/piinput/function_menu.h`、`src/function_menu.cpp`：纯功能菜单状态机。
- `data/help_zh.tsv`：安装包内中文帮助正文。
- `tests/help_catalog_tests.cpp`、`tests/symbol_catalog_tests.cpp`、`tests/recent_symbols_tests.cpp`、`tests/function_menu_tests.cpp`：新增核心测试。

### 修改

- `include/piinput/settings.h`、`src/settings.cpp`：新增 `[commands]` 设置。
- `include/piinput/symbols.h`、`src/symbols.cpp`：分类浏览和用户覆盖。
- `include/piinput/host_session.h`、`src/host_session.cpp`：接入功能菜单。
- `include/piinput/host_protocol.h`、`src/host_protocol.cpp`、`data/host_protocol.json`：协议 v2。
- `include/piinput/host_messages.h`、`src/host_messages.cpp`：菜单显示字段与命令策略。
- `platform/windows/host/*`：加载目录、会话注入和菜单候选显示。
- `platform/windows/tsf/stable_text_service.*`、`composition_mirror.*`：快捷键、v2 快照和安全编辑。
- `CMakeLists.txt`、发布脚本、中文文档和发布清单。

---

### Task 1: 命令设置和默认反引号兼容

**Files:**
- Modify: `include/piinput/settings.h`
- Modify: `src/settings.cpp`
- Modify: `tests/settings_tests.cpp`
- Modify: `src/punctuation.cpp`
- Modify: `tests/test_main.cpp`

**Interfaces:**
- Produces: `enum class CommandHotkey { ctrl_alt_grave, ctrl_grave, disabled };`
- Produces: `struct CommandSettings { bool enabled; CommandHotkey hotkey; bool middle_dot_alias; };`
- Produces: `SettingsSnapshot::commands`。

- [ ] **Step 1: 写设置与反引号失败测试**

在 `tests/settings_tests.cpp` 增加断言：默认 `enabled=true`、`hotkey=ctrl_alt_grave`、`middle_dot_alias=false`；合法值往返；非法 `hotkey` 保留上一份有效值并产生一条错误。于 `tests/test_main.cpp` 增加：

```cpp
check(punctuation.transform('`', PunctuationMode::chinese, false) == "`",
    "Chinese mode preserves Markdown grave by default");
check(punctuation.transform('`', PunctuationMode::chinese, true) == "～",
    "shift grave keeps the Chinese full-width tilde");
```

- [ ] **Step 2: 运行定向测试确认 RED**

```powershell
cmake --build build/windows-x64 --config Release --target piinput-settings-tests piinput-core-tests --parallel 1
```

Expected: 编译失败于缺少 `CommandHotkey`/`SettingsSnapshot::commands`，或反引号断言失败为 `·`。

- [ ] **Step 3: 实现最小设置结构与解析**

```cpp
enum class CommandHotkey : std::uint8_t {
    ctrl_alt_grave,
    ctrl_grave,
    disabled,
};

struct CommandSettings final {
    bool enabled{true};
    CommandHotkey hotkey{CommandHotkey::ctrl_alt_grave};
    bool middle_dot_alias{false};
    bool operator==(const CommandSettings&) const = default;
};
```

在 `settings.cpp` 添加 `[commands]` 解析与默认序列化；把中文标点下未 Shift 的 `` ` `` 改为 ASCII `` ` ``。

- [ ] **Step 4: 运行定向测试确认 GREEN**

```powershell
cmake --build build/windows-x64 --config Release --target piinput-settings-tests piinput-core-tests --parallel 1
ctest --test-dir build/windows-x64 -C Release -R "piinput-(settings|core)-tests" --output-on-failure
```

Expected: 2/2 通过。

- [ ] **Step 5: 提交**

```powershell
git add include/piinput/settings.h src/settings.cpp src/punctuation.cpp tests/settings_tests.cpp tests/test_main.cpp
git commit -m "feat: add command settings and preserve Markdown graves"
```

---

### Task 2: 中文帮助目录

**Files:**
- Create: `include/piinput/help_catalog.h`
- Create: `src/help_catalog.cpp`
- Create: `data/help_zh.tsv`
- Create: `tests/help_catalog_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `HelpLoadResult HelpCatalog::load_tsv(const std::filesystem::path&) noexcept`。
- Produces: `std::vector<HelpEntry> HelpCatalog::children(std::string_view parent_id) const`。
- Produces: `const HelpEntry* HelpCatalog::find(std::string_view stable_id) const noexcept`。

- [ ] **Step 1: 写帮助目录失败测试**

测试 BOM、注释、根目录、两级目录、稳定顺序、重复 ID、缺字段、非法 UTF-8、缺文件。Fixture：

```text
id	parent	order	title	body
basic		10	基本输入	输入拼音或小鹤双拼后选择候选。
basic-space	basic	10	空格选词	按空格选择当前候选。
```

缺文件必须 `loaded=false` 且不抛异常；坏行进入 `errors`，其他行可查询。

- [ ] **Step 2: 运行测试确认 RED**

```powershell
cmake --build build/windows-x64 --config Release --target piinput-help-catalog-tests --parallel 1
```

Expected: 失败于缺少目标或 `piinput/help_catalog.h`。

- [ ] **Step 3: 实现严格、非致命解析器**

```cpp
struct HelpEntry final {
    std::string id;
    std::string parent_id;
    std::uint32_t order{};
    std::string title;
    std::string body;
};

struct HelpLoadResult final {
    bool loaded{};
    std::size_t accepted{};
    std::vector<std::string> errors;
};
```

拒绝空 ID/标题、重复 ID、非十进制顺序、额外字段和非法 UTF-8；按 `order`、`title`、`id` 稳定排序。

- [ ] **Step 4: 写完整中文帮助数据**

`data/help_zh.tsv` 覆盖：基本输入、候选与翻页、中英文切换、全拼与小鹤、标点与符号、词库导入与查询、安装升级卸载、隐私与数据位置。正文不要求用户理解英文符号名称。

- [ ] **Step 5: 运行测试确认 GREEN**

```powershell
cmake --build build/windows-x64 --config Release --target piinput-help-catalog-tests --parallel 1
ctest --test-dir build/windows-x64 -C Release -R piinput-help-catalog --output-on-failure
```

Expected: 1/1 通过，真实帮助文件无错误且八个顶层目录齐全。

- [ ] **Step 6: 提交**

```powershell
git add include/piinput/help_catalog.h src/help_catalog.cpp data/help_zh.tsv tests/help_catalog_tests.cpp CMakeLists.txt
git commit -m "feat: add offline Chinese help catalog"
```

---

### Task 3: 可分类、可覆盖的符号目录

**Files:**
- Modify: `include/piinput/symbols.h`
- Modify: `src/symbols.cpp`
- Create: `tests/symbol_catalog_tests.cpp`
- Modify: `tests/test_main.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `SymbolLoadResult SymbolIndex::load_builtin_tsv(path) noexcept`。
- Produces: `SymbolLoadResult SymbolIndex::load_user_tsv(path) noexcept`。
- Produces: `categories()` 与 `browse(category, limit)`。
- Preserves: `search(query, limit)` 供 CLI/旧测试兼容。

- [ ] **Step 1: 写符号目录失败测试**

读取真实 `data/symbols.tsv`，断言恰好 140 条并逐类校验数量。用户 fixture 覆盖：同文本替换内置名称/分类/顺序、`enabled=0` 隐藏、最后一条有效重复获胜、控制字符/空名称/越界顺序/额外列拒绝、多字符短语可用。

- [ ] **Step 2: 运行测试确认 RED**

```powershell
cmake --build build/windows-x64 --config Release --target piinput-symbol-catalog-tests --parallel 1
```

Expected: 编译失败于新目录接口不存在。

- [ ] **Step 3: 扩展符号模型并保持旧表兼容**

```cpp
enum class SymbolSource : std::uint8_t { builtin, user };

struct SymbolCandidate final {
    std::string stable_id;
    std::string symbol;
    std::string category;
    std::string name;
    std::vector<std::string> aliases;
    std::int32_t order{};
    bool enabled{true};
    SymbolSource source{SymbolSource::builtin};
    int score{};
};
```

内置稳定 ID 为 `builtin:<category>:<symbol>`；用户稳定 ID 为 `user:<symbol>`。用户同文本覆盖内置同文本。用户表严格五列：文本、中文名称、分类、排序、启用。

- [ ] **Step 4: 将致命加载改为单条隔离**

两个 load API 均 `noexcept` 返回错误列表；缺失用户文件等同空文件。内置文件缺失返回 `loaded=false`，但不抛异常。

- [ ] **Step 5: 运行定向测试确认 GREEN**

```powershell
cmake --build build/windows-x64 --config Release --target piinput-symbol-catalog-tests piinput-core-tests --parallel 1
ctest --test-dir build/windows-x64 -C Release -R "piinput-(symbol-catalog|core)-tests" --output-on-failure
```

Expected: 全部通过，旧符号搜索测试仍通过。

- [ ] **Step 6: 提交**

```powershell
git add include/piinput/symbols.h src/symbols.cpp tests/symbol_catalog_tests.cpp tests/test_main.cpp CMakeLists.txt
git commit -m "feat: add browsable and user-extensible symbol catalog"
```

---

### Task 4: 最近使用符号的最小化存储

**Files:**
- Create: `include/piinput/recent_symbols.h`
- Create: `src/recent_symbols.cpp`
- Create: `tests/recent_symbols_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `RecentLoadResult RecentSymbols::load(path) noexcept`。
- Produces: `record(stable_id, unix_seconds) noexcept`、`ordered(limit)`、`dirty()`、`save_atomic(path, error) noexcept`。

- [ ] **Step 1: 写隐私、排序与原子保存失败测试**

测试只允许 `stable_id<TAB>count<TAB>last_used` 三列；不出现正文、应用名或光标上下文。排序为 `last_used desc`、`count desc`、`stable_id asc`；重复 ID 合并；损坏文件隔离为 `.corrupt`；保存先写同目录临时文件再原子替换。

- [ ] **Step 2: 运行测试确认 RED**

```powershell
cmake --build build/windows-x64 --config Release --target piinput-recent-symbols-tests --parallel 1
```

Expected: 失败于缺少头文件或目标。

- [ ] **Step 3: 实现最近记录**

```cpp
struct RecentSymbolEntry final {
    std::string stable_id;
    std::uint64_t count{};
    std::int64_t last_used{};
};
```

计数饱和于 `uint64_t` 最大值；负时间、零计数、额外字段和非法 UTF-8 视为坏行。所有异常在 API 边界转换为错误结果。

- [ ] **Step 4: 运行测试确认 GREEN**

```powershell
cmake --build build/windows-x64 --config Release --target piinput-recent-symbols-tests --parallel 1
ctest --test-dir build/windows-x64 -C Release -R piinput-recent-symbols --output-on-failure
```

Expected: 1/1 通过。

- [ ] **Step 5: 提交**

```powershell
git add include/piinput/recent_symbols.h src/recent_symbols.cpp tests/recent_symbols_tests.cpp CMakeLists.txt
git commit -m "feat: add private recent-symbol history"
```

---

### Task 5: 独立功能菜单状态机

**Files:**
- Create: `include/piinput/function_menu.h`
- Create: `src/function_menu.cpp`
- Create: `tests/function_menu_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `HelpCatalog`、`SymbolIndex`、`RecentSymbols`。
- Produces: `FunctionMenu::open_root()`、`apply(FunctionMenuCommand)`、`snapshot()`、`close()`。

- [ ] **Step 1: 写纯状态机失败测试**

覆盖：根菜单只含“符号中心、使用帮助”；帮助逐级进入与 Backspace 返回；符号分类、最近使用、我的符号；数字/候选 ID 选择；Esc 顶层关闭；旧 generation ID 拒绝；选择符号只返回符号文本；帮助正文选择不提交正文。

- [ ] **Step 2: 运行测试确认 RED**

```powershell
cmake --build build/windows-x64 --config Release --target piinput-function-menu-tests --parallel 1
```

Expected: 失败于缺少 `piinput/function_menu.h`。

- [ ] **Step 3: 实现页面和动作类型**

```cpp
enum class FunctionMenuCommand : std::uint8_t {
    open,
    back,
    cancel,
    choose,
};

struct FunctionMenuCandidate final {
    std::uint64_t stable_action_id{};
    std::string display_text;
};

struct FunctionMenuSnapshot final {
    bool visible{};
    std::uint64_t generation{};
    std::string prompt;
    std::vector<FunctionMenuCandidate> candidates;
    std::optional<std::string> commit_text;
};
```

内部页面使用显式 enum，不从显示文本反推行为。帮助正文为不可提交页面；符号显示 `符号 + 空格 + 中文名称`，提交只含符号。

- [ ] **Step 4: 实现最近记录联动**

成功选择符号后只调用内存内的 `RecentSymbols::record`；即使后续写盘失败，也必须返回符号文本。`FunctionMenu` 不持有文件路径、不直接写磁盘。

- [ ] **Step 5: 运行测试确认 GREEN**

```powershell
cmake --build build/windows-x64 --config Release --target piinput-function-menu-tests --parallel 1
ctest --test-dir build/windows-x64 -C Release -R piinput-function-menu --output-on-failure
```

Expected: 1/1 通过。

- [ ] **Step 6: 提交**

```powershell
git add include/piinput/function_menu.h src/function_menu.cpp tests/function_menu_tests.cpp CMakeLists.txt
git commit -m "feat: add deterministic help and symbol menu state"
```

---

### Task 6: Host 协议 v2 分离正文组合与候选菜单

**Files:**
- Modify: `include/piinput/host_protocol.h`
- Modify: `src/host_protocol.cpp`
- Modify: `data/host_protocol.json`
- Modify: `include/piinput/host_session.h`
- Modify: `include/piinput/host_messages.h`
- Modify: `src/host_messages.cpp`
- Modify: `tests/host_protocol_tests.cpp`
- Modify: `tests/host_messages_tests.cpp`

**Interfaces:**
- Produces: `host_protocol_v2 = 2U`；编码只生成 v2，解码拒绝 v1。
- Produces: `HostKeyKind::open_command_menu`。
- Produces: `HostSnapshot::candidate_window_visible`、`prompt`、`command_hotkey`、`middle_dot_alias`。

- [ ] **Step 1: 写 v2 消息失败测试**

HostReply round-trip fixture：

```cpp
reply.snapshot.raw = "";
reply.snapshot.candidate_window_visible = true;
reply.snapshot.prompt = "功能菜单";
reply.snapshot.command_hotkey = CommandHotkey::ctrl_alt_grave;
reply.snapshot.middle_dot_alias = false;
```

断言 round-trip 保留字段、未知 flag 拒绝、超长 prompt 拒绝、v1 envelope 返回 `unsupported_version`。

- [ ] **Step 2: 运行测试确认 RED**

```powershell
cmake --build build/windows-x64 --config Release --target piinput-host-protocol-tests piinput-host-messages-tests --parallel 1
```

Expected: 编译失败于缺少字段/枚举，或 v1 拒绝断言失败。

- [ ] **Step 3: 升级有界协议**

保持 56 字节 envelope header；版本常量改为 2。HostReply 固定字段后加入：1 字节菜单可见 flag、1 字节 hotkey、1 字节 alias flag、1 字节保留位，然后写 `prompt`。`prompt` 使用现有字符串上限并计入 1 MiB payload 限制。

- [ ] **Step 4: 更新协议元数据**

`data/host_protocol.json` 写 `"protocol_version": 2`。稳定 CLSID/Profile GUID 和管道安全策略不变。

- [ ] **Step 5: 运行测试确认 GREEN**

```powershell
cmake --build build/windows-x64 --config Release --target piinput-host-protocol-tests piinput-host-messages-tests --parallel 1
ctest --test-dir build/windows-x64 -C Release -R "piinput-host-(protocol|messages)" --output-on-failure
```

Expected: 2/2 通过。

- [ ] **Step 6: 提交**

```powershell
git add include/piinput/host_protocol.h src/host_protocol.cpp data/host_protocol.json include/piinput/host_session.h include/piinput/host_messages.h src/host_messages.cpp tests/host_protocol_tests.cpp tests/host_messages_tests.cpp
git commit -m "feat: separate candidate menus from TSF composition"
```

---

### Task 7: Host 会话与运行时接线

**Files:**
- Modify: `src/host_session.cpp`
- Modify: `tests/host_session_tests.cpp`
- Modify: `platform/windows/host/host_runtime.h/.cpp`
- Modify: `tests/host_runtime_tests.cpp`
- Modify: `platform/windows/host/session_manager.h/.cpp`
- Modify: `tests/session_manager_tests.cpp`
- Modify: `platform/windows/host/main.cpp`
- Modify: `platform/windows/host/pipe_server.h/.cpp`

**Interfaces:**
- Consumes: Tasks 1–6 的设置、目录、最近记录、菜单和 v2 快照。
- Produces: 菜单期间 `snapshot.raw/caret` 等于进入前组合；`prompt/candidates` 来自菜单。

- [ ] **Step 1: 写 Host 会话失败测试**

空组合打开后：`raw.empty()`、`candidate_window_visible=true`、`prompt=="功能菜单"`。已有 `wo` 组合打开后仍 `raw=="wo"`，候选换为菜单；Esc 恢复“我、窝……”候选。帮助不提交；符号提交后恢复进入前组合并返回 `HostAction::commit_and_update`。

- [ ] **Step 2: 增加原子“提交并恢复组合”动作**

在 `HostAction` 加 `commit_and_update`：HostReply `text` 是符号，snapshot `raw/caret` 是恢复组合。补消息 round-trip 和未知动作测试。

- [ ] **Step 3: 运行 Host 测试确认 RED**

```powershell
cmake --build build/windows-x64 --config Release --target piinput-host-session-tests piinput-session-manager-tests --parallel 1
```

Expected: 菜单事件或 `commit_and_update` 缺失导致失败。

- [ ] **Step 4: 接入 FunctionMenu**

`HostSession` 保存进入菜单前的 `HostResumeState`。菜单导航只推进 generation，不改 `ImeSession/EnglishSession`；符号提交关闭菜单并恢复原组合。候选 ID 继续编码当前 generation。

- [ ] **Step 5: 加载数据并降级失败**

`HostRuntime` 加载：

```text
package_data/help_zh.tsv
package_data/symbols.tsv
user_data/symbols_user.tsv
user_data/recent_symbols.tsv
```

缺失/损坏只写 `UserData/logs/catalogs.log`，不让中文引擎启动失败。首次缺少用户符号文件时，以临时文件 + 原子替换创建带注释和表头的示例文件。

- [ ] **Step 6: SessionManager 注入共享目录**

把 `HelpCatalog*`、`SymbolIndex*`、`RecentSymbols*` 传给新会话。每个菜单保存自己的页面状态，最近记录由 Host 主线程串行更新。`PipeServer` 在成功把选词回复写回 Shim 后检查 `RecentSymbols::dirty()`，再执行原子保存；因此磁盘 I/O 不增加按键到候选/提交回复的等待时间。

- [ ] **Step 7: 运行定向测试确认 GREEN**

```powershell
cmake --build build/windows-x64 --config Release --target piinput-host-session-tests piinput-session-manager-tests piinput-host-runtime-tests --parallel 1
ctest --test-dir build/windows-x64 -C Release -R "piinput-(host-session|session-manager|host-runtime)" --output-on-failure
```

Expected: 3/3 通过。

- [ ] **Step 8: 提交**

```powershell
git add src/host_session.cpp tests/host_session_tests.cpp platform/windows/host/host_runtime.h platform/windows/host/host_runtime.cpp tests/host_runtime_tests.cpp platform/windows/host/session_manager.h platform/windows/host/session_manager.cpp tests/session_manager_tests.cpp platform/windows/host/main.cpp platform/windows/host/pipe_server.h platform/windows/host/pipe_server.cpp include/piinput/host_session.h include/piinput/host_messages.h src/host_messages.cpp tests/host_messages_tests.cpp
git commit -m "feat: integrate help and symbols into host sessions"
```

---

### Task 8: Windows Shim 快捷键、组合镜像和候选显示

**Files:**
- Modify: `platform/windows/tsf/composition_mirror.h/.cpp`
- Modify: `tests/composition_mirror_tests.cpp`
- Modify: `platform/windows/tsf/stable_text_service.h/.cpp`
- Modify: `platform/windows/host/candidate_presenter.cpp`
- Modify: `platform/windows/host/pipe_server.cpp`
- Modify: `tests/candidate_presenter_tests.cpp`
- Modify: `tests/windows_source_regression.cmake`

**Interfaces:**
- Consumes: v2 `HostSnapshot` 和 `HostAction::commit_and_update`。
- Produces: Shim 缓存 Host 返回的 `CommandHotkey/middle_dot_alias`，只吞明确匹配的快捷键。

- [ ] **Step 1: 写组合镜像和候选显示失败测试**

断言菜单回复更新 `confirmed_snapshot_`，但不把 `prompt` 复制进 `raw_`；`candidate_window_visible=true` 且 `raw.empty()` 时 presenter 仍显示，并用 `prompt` 作标题；菜单关闭时隐藏。

- [ ] **Step 2: 写快捷键源回归失败门禁**

门禁要求：

```text
Ctrl+Alt+VK_OEM_3 → HostKeyKind::open_command_menu
Ctrl+VK_OEM_3 默认不吃
无修饰 VK_OEM_3 → punctuation/raw grave
HostAction::commit_and_update 有独立 edit path
菜单 visible 且 raw 为空时仍发送 caret anchor
```

- [ ] **Step 3: 运行定向测试确认 RED**

```powershell
cmake --build build/windows-x64 --config Release --target piinput-composition-mirror-tests piinput-candidate-presenter-tests PiInputTSF --parallel 1
ctest --test-dir build/windows-x64 -C Release -R "piinput-(composition-mirror|candidate-presenter|windows-source-regression)" --output-on-failure
```

Expected: 至少一个编译或回归门禁失败。

- [ ] **Step 4: 实现策略缓存与精确修饰键检测**

只在 Ctrl+Alt 按下、Shift/Win 未按、键为 `VK_OEM_3` 且策略为 `ctrl_alt_grave` 时吞键；`ctrl_grave` 仅在用户配置且 Alt/Shift/Win 未按时吞键。按键抬起不得触发 Shift 中英文切换。

- [ ] **Step 5: 实现菜单不改正文的镜像路径**

`HostAction::none` 只更新快照和策略；`update` 只写 `snapshot.raw`；`commit_and_update` 在一个 TSF edit session 内结束旧 Composition、提交 `text`、从新插入点创建 Composition 并写 `snapshot.raw/caret`。任一 COM/TSF 步骤失败立即返回 HRESULT，不留半提交状态。

- [ ] **Step 6: 按菜单可见性暂存和定位候选窗**

候选判断从 `!snapshot.raw.empty()` 改为 `snapshot.candidate_window_visible`。标题使用 `prompt.empty() ? raw : prompt`；仍优先 TSF 文本光标，应用不提供坐标时才使用既有 GUI caret/鼠标回退。

- [ ] **Step 7: 运行定向测试确认 GREEN**

```powershell
cmake --build build/windows-x64 --config Release --target PiInputTSF PiInputHost piinput-composition-mirror-tests piinput-candidate-presenter-tests --parallel 1
ctest --test-dir build/windows-x64 -C Release -R "piinput-(composition-mirror|candidate-presenter|windows-source-regression)" --output-on-failure
```

Expected: 3/3 通过，Shim 与 Host Release 编译成功。

- [ ] **Step 8: 提交**

```powershell
git add platform/windows/tsf/composition_mirror.h platform/windows/tsf/composition_mirror.cpp tests/composition_mirror_tests.cpp platform/windows/tsf/stable_text_service.h platform/windows/tsf/stable_text_service.cpp platform/windows/host/candidate_presenter.cpp platform/windows/host/pipe_server.cpp tests/candidate_presenter_tests.cpp tests/windows_source_regression.cmake
git commit -m "feat: open host menus without changing document text"
```

---

### Task 9: 可选中文间隔号别名

**Files:**
- Modify: `include/piinput/host_session.h`
- Modify: `src/host_session.cpp`
- Modify: `tests/host_session_tests.cpp`
- Modify: `platform/windows/tsf/stable_text_service.cpp`
- Modify: `tests/windows_source_regression.cmake`

**Interfaces:**
- Consumes: `CommandSettings::middle_dot_alias`。
- Produces: 仅在用户显式启用后识别 `··f/h/u`；默认路径不缓存反引号。

- [ ] **Step 1: 写默认关闭测试**

默认设置下发送三个反引号标点事件，断言三次均提交 ASCII `` ` ``；随后输入 `f` 正常进入普通输入，不打开菜单。

- [ ] **Step 2: 写启用后的精确匹配测试**

启用后，Shim 把中文标点模式下无修饰 `VK_OEM_3` 映射为 `HostKeyKind::middle_dot_prefix`。两次 prefix 后 `f/F` 打开符号、`h/H` 打开帮助；未知字母通过 `commit_and_update` 提交 `··` 并把字母作为新组合；Esc 清空 pending；切应用不写残缺字符。

- [ ] **Step 3: 运行测试确认 RED**

```powershell
cmake --build build/windows-x64 --config Release --target piinput-host-session-tests PiInputTSF --parallel 1
```

Expected: 失败于缺少 `middle_dot_prefix` 或别名状态。

- [ ] **Step 4: 实现有界两字符状态**

Host 只保存 `0/1/2` 三种 pending 计数，不用定时器。第三个 prefix 不触发功能，原样提交三个 `·`；Backspace 逐个删除；不匹配键先原子提交 pending，再执行普通操作。

- [ ] **Step 5: 运行测试确认 GREEN**

```powershell
cmake --build build/windows-x64 --config Release --target piinput-host-session-tests PiInputTSF --parallel 1
ctest --test-dir build/windows-x64 -C Release -R "piinput-(host-session|windows-source-regression)" --output-on-failure
```

Expected: 2/2 通过，默认 Markdown 回归仍通过。

- [ ] **Step 6: 提交**

```powershell
git add include/piinput/host_session.h src/host_session.cpp tests/host_session_tests.cpp platform/windows/tsf/stable_text_service.cpp tests/windows_source_regression.cmake
git commit -m "feat: add opt-in middle-dot command aliases"
```

---

### Task 10: 发布、中文文档与全量验证

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `scripts/windows/package-release.ps1`
- Modify: `tests/release_metadata_regression.cmake`
- Modify: `tests/stable_runtime_tests.cpp`
- Modify: `README.md`
- Modify: `PROJECT_CONTEXT.md`
- Modify: `docs/安装与使用指南.md`
- Create: `docs/release_notes_v0.4.2-dev.md`
- Create: `docs/VERIFICATION_v0.4.2-dev.md`
- Modify: `RELEASE_MANIFEST.md`
- Modify: `FILE_LIST.txt`
- Modify: `SHA256SUMS.txt`

**Interfaces:**
- Produces: `PiInput-v0.4.2-dev-windows-x64.zip`，包含帮助、符号、自定义示例和使用指南。

- [ ] **Step 1: 加入安装与发布门禁**

CMake 安装必须包含：

```text
data/help_zh.tsv
data/symbols.tsv
docs/安装与使用指南.md
```

发布回归解压 ZIP 后检查这三项、安装器、卸载器、Shim、Host 和完整词库。

协议 v2 会使稳定 Shim 二进制发生一次变化。补安装器回归：旧 Shim 与新 Shim 字节不同且目标 DLL 未占用时必须原子替换；目标被占用时必须明确失败且不得切换到 v2 Host。发布说明要求用户在这种唯一一次的稳定入口升级中重启 Windows 后重新运行安装器，普通后续 Host/词库升级仍不要求关闭应用。

- [ ] **Step 2: 更新中文使用文档**

安装指南写明：默认 `Ctrl+Alt+反引号`；`Ctrl+反引号` 留给 VS Code；Markdown 单/双/三反引号默认直通；如何浏览 140 个符号；如何编辑 `%LOCALAPPDATA%\PiInput\UserData\symbols_user.tsv`；恢复默认；用户数据默认不随卸载删除。

- [ ] **Step 3: 运行格式和元数据检查**

```powershell
git diff --check
ctest --test-dir build/windows-x64 -C Release -R piinput-release-metadata-regression --output-on-failure
```

Expected: 无空白错误，元数据目标通过。

- [ ] **Step 4: Fresh Release 全目标编译**

```powershell
cmake --build build/windows-x64 --config Release --parallel 1
```

Expected: exit 0；Shim、Host、安装器、卸载器和测试目标全部生成。

- [ ] **Step 5: Fresh 全量 CTest**

```powershell
ctest --test-dir build/windows-x64 -C Release --output-on-failure --no-tests=error
```

Expected: 0 failures；包含既有全拼、小鹤 406 合法码、3500/7000 汉字、长句、分段取字、英文、标点、安装卸载、Host 重启、候选光标，以及新增帮助/符号/菜单测试。

- [ ] **Step 6: 生成并解压验证 ZIP**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/windows/package-release.ps1 -Configuration Release
```

解压到临时目录，检查必须文件，并比较 ZIP 内 Shim/Host/Installer 与 staging 的 SHA-256。

- [ ] **Step 7: 写真实验证结果并刷新清单**

`docs/VERIFICATION_v0.4.2-dev.md` 记录编译命令、CTest 总数/耗时、ZIP 路径/SHA-256、未自动安装事实和手工测试场景。按仓库机制从 tracked + untracked nonignored files 重建 `FILE_LIST.txt` 和 `SHA256SUMS.txt`，排除 `.git/build/dist/artifacts` 与 SHA 文件自身。

- [ ] **Step 8: 再跑发布门禁并提交**

```powershell
ctest --test-dir build/windows-x64 -C Release -R "piinput-(sha256|release-metadata|windows-source-regression)" --output-on-failure
git diff --check
git add CMakeLists.txt scripts/windows/package-release.ps1 tests/release_metadata_regression.cmake tests/stable_runtime_tests.cpp README.md PROJECT_CONTEXT.md docs/安装与使用指南.md docs/release_notes_v0.4.2-dev.md docs/VERIFICATION_v0.4.2-dev.md RELEASE_MANIFEST.md FILE_LIST.txt SHA256SUMS.txt
git commit -m "release: package PiInput v0.4.2 help and symbol center"
```

Expected: 3/3 发布门禁通过；不自动安装，不推送远程。

---

## 计划自检

- 规格第 2 节入口与 Markdown：Tasks 1、6、8、9。
- 规格第 3 节中文帮助：Tasks 2、5、7、10。
- 规格第 4 节符号、最近和用户扩展：Tasks 3、4、5、7、10。
- 规格第 5 节模块边界：Tasks 5–8。
- 规格第 6 节失败降级：Tasks 2–4、7。
- 规格第 7 节测试：每个任务均为 RED→GREEN，Task 10 全量门禁。
- 规格第 8 节发布文档：Task 10。
- 类型一致性：`CommandHotkey`、`FunctionMenuSnapshot`、`HostSnapshot` 和 `HostAction::commit_and_update` 只在首次定义后被后续任务复用。
- 范围控制：拆字算法和 YeSymbol 数据接入未混入本计划。
