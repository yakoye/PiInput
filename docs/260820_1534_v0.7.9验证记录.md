# PiInput v0.7.9 验证记录

## 问题

v0.7.8 把「中/英」做成第二个通知区域图标。实机结果是三个图标：溢出区里 `[中][π]` 两个，任务栏上另有一个 `π`。用户给出搜狗、微软拼音、微信输入法的截图，它们的中/英与产品图标都是**紧挨着的一组**。

## 他们的做法

那不是通知区域，而是**任务栏的输入指示器**。搜狗与微软拼音通过 TSF 语言栏（`ITfLangBarItemButton`）注册按钮，由 Windows 在指示器内绘制并排列。微软拼音的「中 拼」就是两个这样的按钮。

`Shell_NotifyIcon` 属于任务栏的另一块区域，与输入指示器互不相干，因此无论加多少个图标都拼不成一组——这正是 v0.7.8 的错误所在。

## 实现

新增 `platform/windows/tsf/lang_bar_item.cpp`：

- `LangBarButton` 实现 `ITfLangBarItemButton` 与 `ITfSource`，两个固定 GUID 的按钮，`TF_LBI_STYLE_SHOWNINTRAY` 使其显示在指示器中；
- 语言按钮通过 `GetText` 返回「中」或「英」。**文字交给 Windows 绘制**，因此自动跟随系统主题（浅色黑字、深色白字）、背景透明。自绘图标做不到这一点，这也是弃用自绘的原因；
- 产品按钮为 `TF_LBI_STYLE_BTN_MENU`，`InitMenu` 提供菜单，`OnMenuSelect` 分发；
- 状态变化经 `ITfLangBarItemSink::OnUpdate` 通知 Windows 重绘。

语言栏活在 TSF DLL 内，即宿主应用进程中，因此菜单命令由文本服务直接处理：设置与符号用 `ShellExecuteW` 启动同目录程序，方案切换写 `settings.ini`（Host 在下一个输入边界重新读取）。

TSF DLL 不链接引擎库，只需要 `schema` 与 `symbol_tool` 两个值，因此用一个约二十行的逐行查找读取 ini，而不是把整个设置解析器拉进每个宿主进程。

Host 端的通知区域图标与 `tray_icon.*` 已删除。

## yesymbol 随包发布

`yesymbol/yesymbol.exe` 通过 `install(PROGRAMS ...)` 进入 `bin`，与其他程序同目录。语言栏「符号」的查找顺序是：设置中的 `symbol_tool` → 程序目录下的 `yesymbol.exe`。安装器不强制要求该文件存在，旧包缺少它时会提示如何设置。

## 一处测试环境问题

`piinput-settings-window-regression` 曾失败，报「窗口立即退出，退出码 0」。排查发现是环境问题而非代码缺陷：上一次测试遗留了两个设置窗口进程（其中一个来自前一天），新实例正确判定「已有实例」，把已有窗口拉到前台后退出——与「窗口打不开」的表现完全相同。

测试已加固：运行前先结束遗留的同名进程。

## 自动验证

- Windows x64 Release 全目标构建：通过；
- 完整 CTest：60/60 通过。

## 人工验证要点

见 `v0.7.9安装、使用与测试.md`。核心是：任务栏输入指示器里应只有「中 π」一组，通知区域不再有 PiInput 图标；切到深色主题时「中」应变成白字。
