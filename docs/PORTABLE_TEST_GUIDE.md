# PiInput 免安装测试包说明

这个包只用于验证输入核心，不安装输入法，不注册 TSF，不需要管理员权限，也不会弹出 UAC。

## 使用方法

1. 完整解压 ZIP。
2. 双击 `Run-Portable-Tests.cmd`。
3. 自动测试结束后会打开 `PiInput-Test.exe`，可在测试文本框中继续试全拼、小鹤双拼和英文候选。
4. 自动结果保存在同目录的 `Portable-Test-Result.txt`。

## 自动检查内容

- Host 输入状态机和候选选择；
- 双反引号加 `f` 的符号中心入口，以及分号命令已移除；
- 单反引号、Markdown 行内代码和三个反引号代码块保持原样；
- 全拼 `ganjue` 与小鹤 `gjjt` 命中“感觉”；
- `sheshidu` 命中 `℃`；
- 真实二进制词库的 1000 次热查询性能门禁。

## 当前明确未包含

- 中文帮助和拆字功能尚未实现，也不预留任何分号前缀；
- Notepad4、Notepad++、Word 的真实 TSF 测试必须等下一次使用修正后的安装器做干净安装后执行。免安装包不会假装已经验证这些系统集成项目。

## 安全边界

这个包不包含 `PiInput-Install.exe`、`PiInput-Uninstall.exe`、`PiInputTSF.dll`、`PiInputHost.exe` 或注册工具。运行它不会改变 Windows 输入法列表，也不会影响搜狗、微信或微软拼音。
