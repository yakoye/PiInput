# PiInput v0.7.8 验证记录

## #1 候选窗右键菜单不消失

**根因**：候选窗以 `WS_EX_NOACTIVATE` 创建，并在 `WM_MOUSEACTIVATE` 返回 `MA_NOACTIVATE`，因此永远不会成为前台窗口。`TrackPopupMenuEx` 的文档明确要求宿主窗口先成为前台窗口，否则菜单不响应自身以外的点击，也就永远不会自行关闭。

**修复**：`CandidateWindow` 另建一个隐藏的普通窗口 `menu_owner_` 托管弹出菜单，弹出前 `SetForegroundWindow`，返回后按文档补一条 `WM_NULL`。点击外部与 Esc 都恢复正常。

## #2 鼠标左键点选候选

原来 `WM_LBUTTONUP` 只处理工具栏区域，候选区域没有命中判定。

**跨进程约束**：Host 知道点中了哪个候选，但把文字送进应用只有输入法端能做。因此新增 `host_select_candidate_message`：Host 解析出候选 id 后回传给 Shim，Shim 以 `HostKeyKind::select_candidate` 重放——与数字键完全同一条提交路径，不新增第二套上屏逻辑。

命中判定复用右键已有的 `visible_item_rects_` 与 `visible_item_indexes_`，因此展开多行后各行都可点。

## #3 托盘中/英指示

Windows 不允许一个通知图标承载两个元素，因此中/英是**第二个通知图标**，与主图标共用回调窗口。图标上的字用 GDI 按当前状态实时绘制（`CreateIconIndirect`），语言变化时重绘。

这是刻意避开 TSF 语言栏（`ITfLangBarItemButton`）的方案：Windows 11 对传统语言栏的支持已不稳定，而通知图标行为可控。

## #4 托盘「符号」调用外部工具

原实现弹出说明文字，属于理解偏差——用户要的是启动自己的符号程序。

新增设置 `[general] symbol_tool`，托盘「符号」启动它。未配置时先查程序目录下的 `yesymbol.exe`，仍找不到才提示如何设置。设置窗口「标点符号」页新增一个文本框（新的 `Kind::text` 行类型）填写路径。

## 自动验证

- Windows x64 Release 全目标构建：通过；
- 完整 CTest：60/60 通过；
- 设置往返测试新增 `symbol_tool` 一项，与其余选项一同验证读写与文件保全。

## 人工验证要点

见 `v0.7.8安装、使用与测试.md`。四条：右键菜单点外部要消失、左键点候选要上屏、托盘要有两个图标且左边显示中/英、托盘「符号」要能启动配置的程序。
