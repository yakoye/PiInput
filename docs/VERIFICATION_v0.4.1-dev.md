# PiInput v0.4.1-dev 验证记录

验证环境：Windows x64，Visual Studio 18 2026 Build Tools，MSVC 19.51，Windows SDK 10.0.26100.0。

## Release 构建

- `Visual Studio 18 2026` x64 Release 全目标构建：通过；
- `PiInputTSF.dll`、`PiInputHost.exe`：通过；
- `PiInput-Install.exe`、`PiInput-Uninstall.exe`：通过；
- `piinput-diagnostics.exe`、测试台、命令行工具和全部测试目标：通过。

## 完整自动测试

```text
49/49 tests passed
0 tests failed
Total Test time: 154.13 seconds
```

覆盖范围包括：

- TSF 读取选择范围、折叠到插入点、取得活动视图和 `GetTextExt` 坐标；
- caret 固定宽度消息的正常、负坐标、不可用回退、畸形标志、反向矩形、截断和尾随字节；
- Host 先暂存候选，只接受同一会话和同一 generation 的位置；
- 有文本位置时锚定文本插入点，没有位置时使用鼠标回退；
- 407 个全拼合法音节、406 个小鹤合法码、3500/7000 汉字覆盖；
- 全拼、小鹤、未完成前缀、长句、诗词、分段取字、英文、中文标点和全部内置符号；
- Host 真实进程及连续 20 次停止、重启和组合状态恢复；
- 外部约 50 万条词库、真实 SCEL、段落输入与增量性能门禁；
- 安装器、卸载器、稳定运行目录、迁移、回滚、品牌、元数据和 SHA-256 完整性。
- 稳定 Shim 二进制精确比较、缺失/不同文件失败关闭，以及安装器原子刷新源码门禁。

## 图标验证

- 源 ICO 包含 16、20、24、32、40、48、64、128 和 256 像素九种 32 位图标；
- Release `PiInputTSF.dll` 和 staging DLL 提取出的图标均为紫色底、白色 `π`；
- TSF Profile 注册继续引用 DLL 内第一个图标资源，不依赖旁置 ICO 才能显示。

## 系统状态

本轮只生成 Release staging 与用户 ZIP，不自动安装到 Windows 输入法列表，也不推送仓库。用户完整解压后手动运行 `PiInput-Install.exe` 完成真实应用验证。

## 尚需用户手动验证

自动测试不能模拟所有应用的 TSF 布局实现。安装后应在记事本、Notepad4、ChatGPT、Chrome、VS Code 中确认候选窗跟随当前文本光标；在不提供文本几何位置的特殊程序中确认候选仍能显示在鼠标附近。
