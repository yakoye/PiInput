# PiInput

当前开发版本：`v0.3.0-dev`

PiInput 是一个以输入准确、候选稳定、响应迅速为第一目标的轻量中文输入法项目。

- C++20 跨平台输入核心；
- Windows 首发，后续考虑 macOS、Android、iOS；
- 支持全拼和多种双拼，小鹤双拼优先但不是唯一方案；
- 支持导入搜狗 `.scel` 词库；
- 自有中文/英文/程序员标点与快速符号搜索；
- 纯离线输入完整可用；
- 不加入 AI、语音、广告、资讯和内容推荐；
- 跨设备词库、设置、短语和剪贴板同步放在后期，以可关闭、端到端加密为前提。

## v0.3.0-dev：PiInput 改名与安装基线

- 产品、运行时、命名空间、命令和 Windows 产物统一使用 PiInput/piinput 名称；
- 开发安装使用 `Dev/versions/<版本>/bin` 并存目录，由 `current.txt` 与 COM 注册共同标识活动版本；
- 品牌零残留、SHA-256 完整性和安装路径解析纳入自动回归。

## v0.2.0-dev：输入基础完善版

本版本冻结新功能，只完善全拼、小鹤双拼、通用词库、候选排序、检索延迟和测试机制：

- 修正小鹤零声母规则，覆盖 413 个标准普通话音节；
- 加入用户指定的口诀字、常用组合、全拼同词表、非法编码和稳定性回归；
- 新增 `piinput-dictionary-builder.exe`，支持 pinyin-data、phrase-pinyin-data、Rime YAML 和 PiInput TSV；
- 新增可双击的 `update-dictionaries.cmd`，下载、缓存、转换、验证并原子安装词库；
- `dicts` 固定放在源码同级，更新源码不会删除大型词库和用户 SCEL；
- 完整词匹配采用确定性奖励，修复“干觉”压过“感觉”、“先在”压过“现在”；
- 词频使用对数缩放，避免不同长度路径直接相加原始权重；
- 新增 `run-ime-tests.cmd`，一次验证小鹤、全拼、真实 SCEL、候选顺序和 `wo` 延迟。
- 新增 `PiInput-Install.exe`，使用版本并存安装，旧 DLL 被记事本、Explorer 或其他应用占用时也不再覆盖失败；
- 不完整输入可立即给候选：小鹤 `mkt → 明天`、`rug → 如果/入股`，全拼尾音节前缀同样支持；
- 中文标点已接入 TSF，英文和程序员模式保持 ASCII；
- 纳入 `tests/corpus/v0.2.0` 的 407 个标准音节和 786 条结构化用例，按当前能力分层验证。

双击 `update-dictionaries.cmd` 更新词库，双击 `run-ime-tests.cmd` 执行完整输入测试。详细说明见 [词库更新说明](docs/词库更新说明.md) 和 [v0.2.0 验证记录](docs/VERIFICATION_v0.2.0-dev.md)。

## v0.1.6-dev 的关键变化

用户在 Windows 真机运行注册修复脚本时，旧配置文件停用返回 `0x80004005`。旧配置文件不存在或未激活本来就是可接受状态，但 v0.1.5 的 PowerShell 脚本在 `$ErrorActionPreference = "Stop"` 下把它当作致命错误，导致真正的重新注册尚未执行。

v0.1.6 已完成：

- 清理旧 profile 和旧 DLL 改为幂等 best-effort；
- 安装、修复和卸载脚本不再因旧状态缺失而提前终止；
- 使用 `ITfInputProcessorProfileMgr::RegisterProfile` 注册文本服务和语言配置文件；
- 配置文件设置为默认启用，并保持在 Windows 设置界面可见；
- `piinput-profile.exe` 新增 `--register`、`--unregister` 和 `--status`；
- 安装和修复完成后强制检查 `registered=yes` 与 `enabled=yes`；
- 修复脚本完成后刷新 `ctfmon.exe`；
- 增加针对注册流程和脚本幂等性的源码回归测试。

> 当前环境仍无法编译 Windows TSF 目标。v0.1.6 必须在用户的 Visual Studio 2026 环境运行 `setup-dev.cmd`，才能确认 DLL 编译、注册、Windows 设置可见性和真实输入链路。

## v0.1.4-dev 的关键变化

v0.1.3 的 `piinput-preview.exe` 只是输入核心查询窗口，不是 Windows 系统输入法；用户输入小鹤双拼 `jisrji` 后没有候选，还有一个直接原因：当时只导入了电子和计算机专业 SCEL，专业词库中并不保证包含普通词“计算机”。

v0.1.4 已完成两项针对性修复：

1. 新增内置基础词库，始终与用户导入的专业 SCEL 合并；
2. 新增第一版 Windows TSF 文本服务，可注册到 Windows 语言栏并在记事本等应用中测试真正的中文输入。

## v0.1.4-dev 已实现

### 内置基础词库

- 新增 `data/base_lexicon.tsv`；
- 含常用单字、常用词、常用短语和基础技术词；
- 安装时始终生成 `piinput-base.lex`；
- 与所有用户 SCEL 合并生成 `piinput-imported.lex`；
- 即使没有任何 SCEL，仍可以测试全拼和双拼；
- 已增加 `jisuanji → 计算机`、`jisrji → 计算机` 等回归测试。

当前基础词库只是开发起步词库，不代表已经达到正式输入法的通用词库质量。

### Windows TSF 最小输入链路

新增：

```text
PiInputTSF.dll
piinput-profile.exe
```

当前 TSF 基线包括：

- COM 文本服务注册与注销；
- 简体中文语言配置文件注册；
- `ITfKeyEventSink` 按键接入；
- TSF Composition 创建、更新、提交和取消；
- 调用已有全拼/双拼引擎；
- 原生候选窗口；
- 空格选中当前候选；
- 数字键 `1~9` 选词；
- 上下键移动候选；
- PageUp/PageDown 翻页；
- Backspace、Delete、左右键、Home、End；
- Enter 提交原始拼音；
- Esc 取消；
- `;sheshidu` 符号搜索；
- 本地用户选词学习；
- 全拼、小鹤、自然码、微软、智能 ABC 方案切换工具。

> TSF 代码已经加入源码和 Windows 构建目标，但当前开发环境没有 Windows SDK/MSVC，无法在这里完成真实 Windows 编译和应用兼容验证。用户运行本版 `setup-dev.cmd` 是第一轮真实 Windows TSF 验证。

### 改进后的独立预览

`piinput-preview.exe` 仍然保留，用于不注册系统输入法时单独检查引擎：

- 新增输出区；
- 空格、Enter、数字键和双击可把候选写入输出区；
- 不再只复制到剪贴板；
- 无候选时显示明确原因；
- 优先加载合并词库，其次基础词库。

## 推荐目录

```text
C:\Users\color\Downloads\piinput
├── dicts
│   ├── 电子词汇大全【官方推荐】.scel
│   └── 计算机词汇大全【官方推荐】.scel
├── packages
│   └── piinput-v0.1.6-dev.zip
└── piinput-dev
    ├── setup-dev.cmd
    ├── build.cmd
    └── ...
```

版本压缩包内部固定包含一个顶层目录 `piinput-dev`。升级时先删除或改名旧的 `piinput-dev`，再解压新版。不要把新版直接覆盖到带有旧 CMake 缓存的目录。

## Windows 一键构建、安装与注册

在项目根目录运行：

```powershell
.\setup-dev.cmd
```

默认使用小鹤双拼，并依次完成：

```text
自动发现 Visual Studio 和 CMake
→ 清理旧 build/dist
→ Release 构建
→ 自动测试
→ 生成 EXE 与 PiInputTSF.dll
→ 运行 PiInput-Install.exe
→ 安装到 %LOCALAPPDATA%\PiInput\Dev\versions\<版本-构建号>
→ 编译内置基础词库
→ 导入相邻 dicts 中的 SCEL
→ 注册 TSF 文本服务
→ 激活语言配置文件
→ 执行基础查询和注册表检查
```

指定全拼：

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\setup-dev.ps1 -Schema full
```

只构建、不注册 TSF：

```powershell
.\setup-dev.ps1 -SkipTsfRegistration
```

## 真正的系统输入测试

安装成功后：

```text
1. 完全关闭并重新打开记事本；
2. 按 Win+Space；
3. 选择“PiInput 中文输入法（开发版）”；
4. 默认小鹤双拼输入 jisrji；
5. 按空格；
6. 应上屏“计算机”。
```

全拼模式：

```powershell
.\set-schema.cmd full
```

然后重新切换一次输入法，在记事本输入：

```text
jisuanji + Space
```

应上屏：

```text
计算机
```

切回小鹤：

```powershell
.\set-schema.cmd flypy
```

注册修复：

```powershell
.\repair-registration.ps1
```

自动检查：

```powershell
.\verify-windows.ps1
```

卸载但保留用户词库和学习数据：

```powershell
.\uninstall-dev.ps1
```

## 本版 Windows 产物

```text
dist\windows-x64\bin\piinput-scel-converter.exe
dist\windows-x64\bin\piinput-lexicon-compiler.exe
dist\windows-x64\bin\piinput-cli.exe
dist\windows-x64\bin\piinput-benchmark.exe
dist\windows-x64\bin\piinput-preview.exe
dist\windows-x64\bin\piinput-profile.exe
dist\windows-x64\bin\PiInput-Install.exe
dist\windows-x64\bin\PiInputTSF.dll
```

`PiInput-Install.exe` 可以直接双击。它把新 DLL 安装到新的版本目录，切换 COM 注册后保留仍被旧应用占用的旧目录，不会强制关闭用户应用，也不会删除 `%LOCALAPPDATA%\PiInput\UserData`。重新打开目标应用后即加载新版。

## 尚未达到正式发布质量的部分

- 当前仅生成 x64 TSF DLL，32 位应用和 ARM64 尚未覆盖；
- 候选窗仍使用最小 GDI 实现，未完成 DirectWrite/Direct2D、高 DPI、多显示器和深色模式；
- 目前只有单独 Shift 切换中英文，尚无状态栏和设置界面；
- 中文标点已接入；符号搜索需要重新设计不与中文分号冲突的可配置触发键；
- 完整符号面板、收藏、最近和自定义尚未完成；
- 通用词库仍很小，准确率尚未达到可替换成熟输入法的程度；
- 设置 GUI、引擎独立进程、面向正式发行的单文件签名安装包和全面应用兼容测试尚未完成。当前 `PiInput-Install.exe` 是开发版并存安装器。

## 文档入口

- [项目完整上下文](PROJECT_CONTEXT.md)
- [产品定义](docs/01_product_definition.md)
- [总体架构](docs/02_architecture.md)
- [开发任务](docs/03_development_tasks.md)
- [开发约束](docs/04_development_constraints.md)
- [Windows 技术栈](docs/05_windows_technology_stack.md)
- [词库与 SCEL](docs/06_dictionary_and_scel.md)
- [标点与符号](docs/07_symbols_and_punctuation.md)
- [同步规划](docs/08_sync_plan.md)
- [测试与发布](docs/09_testing_and_release.md)
- [新会话续接说明](docs/10_continuation_guide.md)
- [开发流程 Skills 参考](docs/11_superpowers_skills_reference.md)
- [版本更新工作流](docs/RELEASE_WORKFLOW.md)
- [Windows TSF 开发测试](docs/TSF_DEVELOPER_TEST.md)
- [词库更新说明](docs/词库更新说明.md)
- [v0.2.0-dev 版本说明](docs/release_notes_v0.2.0-dev.md)
- [v0.2.0-dev 验证记录](docs/VERIFICATION_v0.2.0-dev.md)
- [v0.2.0 输入基础设计](docs/superpowers/specs/2026-07-18-v0.2.0-输入基础完善设计.md)
- [v0.2.0 实施计划](docs/superpowers/plans/2026-07-18-v0.2.0-输入基础完善实施计划.md)
