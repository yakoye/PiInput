# PiInput 稳定候选窗、焦点与提交设计

## 目标

修复 v0.4.4 中候选窗随每个按键移动、切换窗口后候选不恢复、空格提交失败后状态丢失，以及候选窗观感偏离 v0.3.8 的问题。

## 固定候选几何

- 一次 Composition 第一次显示候选时解析锚点并确定外框；
- 后续字母只更新标题、候选、选中项，不再次调用定位；
- 外框宽度在本次 Composition 内固定；
- 只有用户按 `=`、`-`、上/下键改变可见行数时允许改变高度，左上角保持不变；
- Composition 提交、取消或失焦隐藏后清除几何锁，下一次输入重新定位；
- 初次定位优先系统文本 caret，无法取得时使用鼠标附近位置。

## 焦点恢复

- 失焦时向 Host 发送 focus=false，并立即隐藏该会话候选；
- 重新获得焦点时通过 `ITfThreadMgr::GetFocus` 与 `ITfDocumentMgr::GetTop` 获取真实 `ITfContext`；
- Resume 请求必须保存该 Context，回复到达后在当前文本框恢复 Composition 并发送新的 caret；
- 旧窗口、旧 sequence 和旧 generation 的回复仍然拒绝。

## 空格提交

- Host 的 Space 选词规则保持不变；
- Shim 只有在 `RequestEditSession` 与 `session_result` 都成功后才接受本地 commit；
- 编辑失败时保留 `CompositionMirror` 的原始输入，并向 Host 发送 Resume 恢复会话；
- 恢复成功后候选重新出现，用户可再次按空格，不允许静默吞键或丢失输入。

## 视觉基线

- 继续使用 Segoe UI、18 逻辑像素、`CLEARTYPE_QUALITY`、白底和 v0.3.8 的行高/间距；
- Host 保持 Per-Monitor-V2 感知，字体只按目标显示器 DPI 创建一次；
- 普通输入更新不擦除背景；相同内容完全跳过重画；
- 光标到候选窗的逻辑间距保持 v0.3.8 的 4 像素。

## 验收

- Notepad4 输入 `w`、`wo`、`wojtdddd` 时，候选窗左上角和宽度保持不变；
- Space 上屏第一候选并结束 Composition；
- 切到另一个普通窗口再回 Notepad4，继续输入可以显示候选并上屏；
- `=` 展开时只增加高度，不改变左上角和宽度；
- 100%、125%、150%、200% 缩放下字体清晰，位置贴近文本 caret；
- PiInput-Test 的自由文本框与 Notepad4 都通过同一真实 TSF 链路测试。

