# PiInput 候选窗文本光标定位设计

## 目标

候选窗优先跟随当前活动编辑窗口中的文本插入光标，而不是跟随鼠标。只有目标程序无法提供 TSF 文本光标矩形时，才沿用当前鼠标附近的兜底位置。

同时保持现有稳定入口 DLL + 独立 Host 架构，不把候选窗和词库重新塞回应用进程。

## 已确认的现状

- `PiInputTSF.dll` 是稳定入口，负责 TSF、Composition 和按键转发。
- `PiInputHost.exe` 负责引擎、候选计算和候选窗显示。
- Host 中的 `CandidateWindow::show_near_caret()` 调用 `GetGUIThreadInfo`；现代应用经常不公开传统 Win32 caret，随后代码调用 `GetCursorPos`，因此候选窗会出现在鼠标附近。
- 当前 `piinput_icon.ico`、Release DLL 内嵌图标和安装目录 DLL 提取图标一致，均为紫底白色 π；图标保持不变。

## 方案比较

### 方案 A：TSF 查询坐标并通过现有协议传给 Host（采用）

稳定入口在同步只读 edit session 中取得当前 selection 的 TSF 文本矩形，将坐标作为已经预留的 `caret` 消息发送给 Host。Host 只负责按坐标显示和屏幕边界约束。

优点：坐标来自真正的文本上下文；兼容稳定入口架构；Host 可独立升级；可测试代际和过期坐标。缺点：需要补全 caret 消息负载和一次轻量 IPC。

### 方案 B：继续由 Host 查询 GUI 线程 caret

改动小，但现代 Chromium、WinUI、编辑器等经常没有 `hwndCaret`，正是当前回退到鼠标的根因，不能满足需求。

### 方案 C：把候选窗移回 TSF DLL

容易直接访问 TSF context，但会把 UI、DPI、多屏和绘制逻辑重新放回所有应用进程，破坏稳定入口和无重启升级目标，不采用。

## 数据流

1. Host 返回新的候选快照。
2. 稳定入口在完成 Composition 更新后，通过 `ITfContext::RequestEditSession(TF_ES_SYNC | TF_ES_READ)` 获取只读 edit cookie。
3. edit session 读取当前 selection，折叠到文本插入位置，并通过活动 `ITfContextView::GetTextExt` 得到屏幕坐标。
4. 稳定入口发送带 `session_id`、`generation` 和矩形的 `HostMessageType::caret`。
5. Host 仅在 session 和 generation 与当前候选快照一致时应用坐标，拒绝延迟到达的旧坐标。
6. 有有效文本矩形时，候选窗显示在矩形下方；多显示器和 DPI 约束继续由 Host 处理。
7. TSF 查询失败、返回无效矩形或目标程序不支持文本矩形时，Host 调用现有鼠标附近兜底逻辑。

## 组件边界

- `host_messages`：定义固定宽度的 `HostCaretUpdate` 编解码和严格校验，不依赖 Win32 类型。
- `stable_text_service`：在 TSF edit session 中查询文本矩形，成功或失败都发送一次 caret 更新。
- `pipe_client` / `pipe_server`：传递并确认 caret 消息，不把它误当成 key reply，也不改变 Composition generation。
- `candidate_presenter`：暂存最新候选快照，仅接受同代坐标并显示；无有效文本矩形时使用鼠标兜底。
- `candidate_window`：增加明确的屏幕矩形定位入口，保留现有兜底入口。

## 错误处理

- TSF selection、active view 或 `GetTextExt` 任一步失败：发送 `has_text_caret=false`，Host 使用鼠标附近位置。
- caret 消息截断、坐标溢出、非法矩形或尾随字节：拒绝消息，不影响输入 Composition。
- caret generation 旧于或不同于当前候选快照：忽略，禁止旧坐标把新候选窗拉走。
- IPC 失败：不吞按键、不回滚已成功的文本编辑；候选窗保持上次状态或隐藏。

## 测试与验收

- 编解码测试：有效矩形、无文本矩形、负屏幕坐标、多显示器坐标、截断、非法标志、非法矩形和尾随字节。
- Presenter 测试：同代坐标显示；旧代、未来代和其他 session 坐标不生效；无坐标走鼠标兜底策略。
- TSF 源码/Windows 构建测试：必须通过只读 edit session、selection 和 `GetTextExt` 获取位置；不得把 `GetCursorPos` 作为首选路径。
- 几何测试：96/144/192 DPI，屏幕四角和负坐标副屏均不越出工作区。
- 全量回归：现有全拼、小鹤 406 合法码、3500/7000 汉字、长句、分段取字、词库、英文补全、中文符号、候选分页、安装/卸载、稳定 Host 升级测试全部运行。
- 图标验收：源码 ICO 包含 16、20、24、32、40、48、64、128、256 像素；Release DLL 可提取紫底白色 π 图标。

## 不在本次范围

- 不改变词频、候选排序或输入方案。
- 不改图标样式。
- 不改变一行候选和按 `=` 展开多行的既有交互。
- 不安装到当前系统；交付编译后的安装包，由用户安装测试。
