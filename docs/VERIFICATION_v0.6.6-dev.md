# PiInput v0.6.6-dev 验证记录

## 本版阻断问题

1. 用户报告：在 Claude、Codex 这类空文本框中粘贴内容后立刻打字，粘贴的内容被整段删除。
2. 用户报告：中文输入状态下打不出「in-to」中间的 `-`；
3. 用户报告：候选框第一次弹出的位置和插入光标对不上，要求做到微信输入法那种精度。

## 问题一：粘贴内容被删除

### 定位过程

用户给出的复现步骤是决定性的：**空文本框 + 粘贴 + 立刻打字**。这三个条件同时出现，把范围直接缩到「组合刚建立、文档刚被外部改动」的窗口内。

代码里能删除用户文字的路径只有三条：

1. `insert_text_at_selection()` —— 选区未折叠时 `InsertTextAtSelection` 会替换选中内容。粘贴后选区是折叠的，排除；
2. `apply_composition_edit()` 的异步提交 —— 写的是自己的组合范围，且有 generation 校验，排除；
3. `OnCompositionTerminated()` —— 无条件对组合范围执行 `SetText(edit_cookie, 0U, L"", 0L)`。

第三条与现象完全吻合：Web 编辑器（Claude、Codex 都是 contenteditable）在粘贴后重建文档节点，TSF 随即回调 `OnCompositionTerminated`，此时组合范围已被应用重映射到新插入的内容上，清空操作就落在了用户的粘贴文字上。空文本框最容易触发，因为粘贴后立刻打字时组合才刚建立，范围边界离粘贴内容最近。

这行清空是 v0.5.4 为「点到别处时不要把生拼音留成正文」加的，本身有存在理由，不能简单删掉。

### 修复

只擦除本输入法确实写进去的内容：

```cpp
[[nodiscard]] bool range_holds_exactly(
    ITfRange* const range, const TfEditCookie edit_cookie, const std::wstring& expected) {
    if (range == nullptr || expected.empty()) return false;
    std::wstring actual(expected.size() + 1U, L'\0');
    ULONG fetched = 0U;
    if (FAILED(range->GetText(edit_cookie, 0U, actual.data(),
            static_cast<ULONG>(actual.size()), &fetched))) {
        return false;
    }
    if (fetched > expected.size()) return false;
    actual.resize(fetched);
    return actual == expected;
}
```

故意多读一个字符，用来识别「范围比我们写的内容更长」这一种情况。判定为一致才清空，其余全部不动。

取舍已明确记录在源码注释中：残留一个拼音音节远好于删除用户的粘贴内容。

## 问题二：中文模式打不出 `-`

### 定位过程

`map_key()` 中 `VK_OEM_MINUS` 在组合状态下被排除出标点分支，无条件映射为 `HostKeyKind::previous_row`。`HostSession::apply()` 的 `previous_row` 分支在行没有变化、且不处于分段取字时返回 `reply(false, HostAction::none)`。返回未处理时组合仍然存活，应用即使收到这个键也无法产生正确结果，实际表现就是死键。

### 修复

`-` 只在真的能翻行时才是翻行键：

- Shim 在 `previous_row` 上带出 `event.character = '-'`，`VK_UP` 不带字符；
- Host 在无行可翻、且未离开分段取字时，把事件改写成 `HostKeyKind::punctuation` 重新进入 `apply()`，走既有标点路径：提交当前候选 + 追加 `-`。

方向键上因为不带字符，行为完全不变，仍然返回未处理。

## 问题三：候选框弹出位置

### 定位过程

用户给的第二个测试方法是决定性的：**在满屏文本的 Notepad++ 里随便点一个位置直接打字**。这个场景与第一个场景（新开窗口直接打字）的唯一区别，是同一个 TSF 会话里已经有过一次输入。

对应到 `CandidatePresenterModel::stage()` 的第二个分支：

```cpp
} else if (remembered_session_ == session_id && remembered_caret_.has_text_caret) {
    caret_ = remembered_caret_;
```

这是 v0.6.1 为「提交之后开始下一个词时候选框不要空等一个往返」加的。它假设下一个词就在上一个词右边几个字符处。用户点到别处时这个假设不成立，候选框先弹在上一个词结束的位置，等真实光标回来才跳过去。

新开窗口时 `remembered_caret_` 为空，走的是第三个分支（等待真实光标），所以第一个测试场景本来就相对准确——这也解释了为什么用户说第二个测试方法「最靠谱」。

### 修复

按用户提的思路实现：先读光标，再弹候选。

1. `CaretProbeSession`：一个只做 `capture_composition_caret` 的只读编辑会话；
2. `TextService::probe_selection_caret()`：用 `TF_ES_SYNC | TF_ES_READ` 请求这个会话，把结果作为普通 caret 消息发出。只读会话比读写会话更容易被同步授予；请求失败或读不到位置时，明确发送 `has_text_caret = false`；
3. 调用点在 `OnKeyDown`，条件是中文模式、`HostKeyKind::text`、`mirror_.raw().empty()` 且无挂起上下文——也就是每个词的第一个按键，一个词只做一次；
4. Host 端 `CandidatePresenterModel::remember_caret()`：探测光标带的是按键前的 generation，`apply_caret()` 会因为快照还不存在而拒绝。`pipe_server` 在 `show_at()` 返回 false 时改调 `remember_caret()`，把它存成下一个词的锚点。`has_text_caret = false` 时反过来清空旧锚点；
5. `anchor_locked_`：插入点随每个字母右移，跟着它走会让候选框在词中间横向漂移。锚点在组合建立时取一次，`stage()` 遇到空 `raw` 时释放。

**没有放松 v0.4.2 的既有约束**：权威锚点仍然在成功的组合编辑会话内取得，探测只覆盖「首键到真实光标回来」之间的空档。原来的源码门禁禁止 Shim 里出现任何 `TF_ES_SYNC | TF_ES_READ`，现在改为同时断言两件事：

- 组合编辑会话内的 `capture_composition_caret` 调用必须保留；
- 出现只读会话时，必须存在 `class CaretProbeSession final`，即只读会话只允许用于按键前探测。

### 测试

- `test_a_pre_key_probe_replaces_the_previous_words_anchor`：上一个词的光标在 (400,200)，探测到 (90,640)，断言候选框开在 (90,640)；
- `test_a_probe_that_found_nothing_makes_the_bar_wait`：探测返回 `has_text_caret = false`，断言 `current_caret()` 为空，候选框等待而不是用旧位置；
- `test_the_anchor_does_not_drift_while_the_word_grows`：连续四次推进 generation 并每次把光标右移 12 像素，断言锚点始终停在 (300,500)；提交后新词能在 (700,500) 重新定位。

## 测试

新增两条：

- `test_dash_that_cannot_page_becomes_an_ordinary_dash`：先验证空会话上的方向键上仍然未处理；再输入 `wo` 后按 `-`，断言返回 `commit`、文本为「首选候选 + `-`」、组合区被清空；
- `test_dash_still_pages_the_candidate_rows_when_a_row_exists`：`items_per_row=2`、`visible_rows=1` 下先按 `=` 展开，再按 `-`，断言返回 `update` 且组合区仍然存活。

新增 Windows 源码门禁：`OnCompositionTerminated` 中的清空必须包裹在 `range_holds_exactly(terminated_range` 判断内，禁止退回无条件清空。

## 最终验证结果

- Windows x64 Release 全目标编译：通过；
- 完整 CTest：全部通过，0 失败；
- 发布元数据与源码文件 SHA-256 校验：通过；
- 发布包：`PiInput-v0.6.6-dev-windows-x64.zip`。

## 用户验证重点

1. 在 Claude 或 Codex 的空输入框里粘贴一段文字，**立刻**开始打字，确认粘贴内容仍在；
2. 中文模式下输入 `wo` 后按 `-`，确认连字符能输出；
3. 候选超过一行时按 `-`，确认仍能翻回上一行；
4. 组合状态下按方向键上，确认行为与之前一致；
5. 打开一个满屏文本的 Notepad++，随便点一个位置直接打字，确认第一次弹出的候选框就在插入光标下面；
6. 新开一个 Notepad++ 直接打字，确认第一次弹出位置同样准确；
7. 把一个词打长，确认候选框不跟着光标横向漂移。
