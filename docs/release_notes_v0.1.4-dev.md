# PiInput v0.1.4-dev 版本说明

## 版本目标

解决 v0.1.3 独立预览无普通中文候选的问题，并从“输入核心预览”进入 Windows TSF 系统输入法最小链路开发。

## 问题根因

用户在小鹤双拼模式输入 `jisrji` 后候选为空。双拼解码结果本身是 `ji'suan'ji`，但 v0.1.3 的合并词库只来自电子、计算机专业 SCEL，专业词库没有保证包含普通词“计算机”。此外，v0.1.3 预览窗口只是独立工具，不会向其他 Windows 应用输入中文。

## 新增

### 内置基础词库

- `data/base_lexicon.tsv`；
- 常用单字、词语、短语和基础技术词；
- 安装时编译为 `piinput-base.lex`；
- 始终合并进 `piinput-imported.lex`；
- 没有 SCEL 时仍能使用基础词库；
- 新增全拼和小鹤普通词回归测试。

### Windows TSF

- `PiInputTSF.dll`；
- `piinput-profile.exe`；
- COM class factory；
- 当前用户 COM 注册；
- TSF 文本服务和简体中文语言配置文件注册；
- TSF profile 启用、激活和停用；
- `ITfTextInputProcessor`；
- `ITfKeyEventSink`；
- `ITfCompositionSink`；
- Composition edit session；
- 候选窗口；
- 空格、数字键、方向键、翻页、编辑和取消；
- 符号搜索；
- 本地用户学习。

### Windows 脚本

- `setup-dev.cmd` 构建、安装、词库导入、TSF 注册和检查；
- `set-schema.cmd`；
- `repair-registration.ps1`；
- `verify-windows.ps1`；
- 卸载脚本加入 TSF 注销。

### 独立预览

- 新增输出区；
- 候选可以真正写入输出区；
- 空格、Enter、数字键和双击上屏；
- 无候选原因提示；
- 基础词库回退。

## 修复

- 修复专业词库覆盖场景下普通词完全缺失的问题；
- 修复预览只能显示/复制、不能形成可见中文输入结果的问题；
- TSF 注册失败时回滚 COM 注册；
- TSF 语言配置文件使用系统默认图标，避免引用没有图标资源的 DLL。

## 已验证

- Linux Release 编译；
- 跨平台核心自动测试；
- 内置基础词库全拼测试；
- 内置基础词库小鹤双拼测试；
- 真实 SCEL 解析回归；
- ASan/UBSan；
- TSF 候选窗口和 DLL 注册源文件的受限语法检查。

## 尚未验证

- Windows MSVC 对新 TSF 目标的真实编译；
- `regsvr32` 和 TSF profile 注册；
- Win+Space 可见性；
- 记事本真实 Composition、候选和上屏；
- 其他 Windows 应用兼容性。

这些必须由用户 Windows 环境运行 `setup-dev.cmd` 后验证，不能在当前 Linux 环境中虚构通过结果。
