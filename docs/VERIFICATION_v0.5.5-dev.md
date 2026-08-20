# PiInput v0.5.5-dev 验证记录

验证日期：2026-08-15  
平台：Windows x64、Visual Studio 18 2026、MSVC、Windows SDK 10.0.26100.0  
状态：Release 构建和自动化测试通过；真实应用人工验收未执行

## 构建

```text
build.ps1 -Configuration Release -SkipTests
```

Release 全目标构建与安装布局生成成功。已生成并检查：

- `PiInputTSF.dll`
- `PiInputHost.exe`
- `PiInput-Install.exe`
- `PiInput-Uninstall.exe`
- `PiInput-Settings.exe`
- `PiInput-Test.exe` 对应的 `piinput-preview.exe`
- 诊断、词库转换、编译、查询和性能工具
- `piinput-base.lex`、符号表、英文词库和 Host 协议描述

## 自动化测试

最终完整 CTest：`56/56 passed`，`0 failed`，总耗时 `47.78 s`。

覆盖重点：

- 候选 5～9 项每行、翻行回到第一列、首尾不循环；
- 正常真实词语页先于分段取字页；
- 分段保留、撤销、统一上屏和旧候选 ID 失效；
- TSF 成功确认后才学习，失败、重复和过期确认不学习；
- 一次进入第一行、两次向前三移动、三次竞争首选；
- 跨窗口新 generation 立即看到学习，旧 generation 不跳变；
- 固定、取消固定、删除、候选左移和 suppression tombstone；
- 旧 4～6 列与新 7 列用户模型、损坏行隔离、并发原子保存；
- Host v3 协议、重启交接、诊断、安装/卸载布局；
- 全拼、小鹤、真实 SCEL、大词库、段落语料和 7000 字覆盖。

## 性能

10,000 条用户词夹具：

```text
用户词精确查询：P50 0.5 us，P95 0.9 us，P99 1.0 us
内存学习更新：P50 0.3 us，P95 0.6 us，P99 0.8 us
候选翻行：P50 0.0 us，P95 0.1 us，P99 0.1 us
```

101,470 条、约 16.7 MB 的真实二进制基础词库，`wo` 热查询 10,000 次：

```text
加载：124.582 ms
平均：31.629 us
P50：27.200 us
P95：56.200 us
P99：104.800 us
```

上述数值是核心/Host 内部门禁，不冒充真实应用从物理按键到窗口绘制的端到端数据。

## 未执行

本轮没有安装到当前 Windows 输入法列表，也没有在 Notepad++、Notepad4、Word、Chrome 或 Codex 中做人工验收。原因是用户要求当前阶段先交付编译内容自行安装测试；自动测试不能替代该步骤。

## 发布完整性

- `FILE_LIST.txt` 与 `SHA256SUMS.txt` 覆盖 467 个源文件，不包含 `build/`、`dist/`、`artifacts/`、外部 `dicts/` 或用户数据；
- Windows ZIP 已生成，根目录同时包含安装器、卸载器、测试台和 Markdown 使用说明；
- 包内 `bin/` 包含稳定 TSF shim、独立 Host、设置、诊断和词库工具；
- 包内 `data/` 包含二进制基础词库、符号、英文数据和 Host v3 协议描述；
- 便携测试包回归通过，确认便携包不包含安装器、TSF DLL 或 Host，并输出 `system_registration=NOT_TOUCHED`；
- `git diff --check` 和最终 SHA-256 门禁在发布收尾阶段重新执行。
