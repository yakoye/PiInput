# PiInput v0.6.3-dev 验证记录

## 本版阻断问题

用户对照搜狗提出四项：

1. 分段取字选中一个字后，候选回到整词行，没有前进到下一个字；
2. 候选框希望默认五行；
3. 候选编号位没有预留，非活动行的文字与编号行对不齐；
4. 「中文符号中文符号中文」在标点之后继续输入仍有延迟。

## 一、分段取字选字后停在原地

### 定位

`HostSession::choose()` 在分段取字暂存成功、且仍有未定音节时执行 `advance_generation(true)`，其中 `rebuild_candidate_grid(collapse_view=true)` 会 `candidate_grid_.reset()`，把选择位置归零。

v0.6.2 之前索引 0 附近就是逐字候选，归零看起来正常；v0.6.2 加入「保留已见整词行」后，索引 0 变成保留的整词，于是暂存一个字之后落回整词行。这是上一版引入的回归。

### 修复

新增 `HostSession::select_first_segment_candidate()`，把「跳到追加段第一个候选」的逻辑收敛为一处，进入分段取字与暂存一个字都调用它。暂存后选择位置落在刚空出来的音节上。

### 回归

`host_session_tests` 新增 `test_staging_a_character_advances_to_the_next_syllable`：`kadun` 展开后选「卡」，断言仍处于分段取字、组合区以「卡」开头、且选择位置落在「顿」。

## 二、候选框默认五行

`CandidateSettings::visible_rows` 默认 3 → 5。同步更新：内置默认设置文本、原生安装器写入的 `settings.ini`、`install-dev.ps1` 的三处补写、`set-candidate-page-size.ps1` 的默认值与取值范围、`CandidateVisualSettings::visible_rows`。

相关断言同步更新：默认值断言、缺省设置文件断言、设置写入内容断言、组合边界前的挂起断言。`test_candidate_screen_size_uses_fixed_dimension_fallback_priority` 的数值按新默认重新设计——它要求 `max_items >= items_per_row * visible_rows`，原来的 `max_items=20` 在 6×5 下已不可能满足，改为 `previous=45`、输入 `max_items=30 / items_per_row=9 / visible_rows=6`，回退顺序（先 visible_rows 后 items_per_row）与错误条数不变。

已有 `settings.ini` 中的显式 `visible_rows=3` 仍然覆盖默认值。

## 三、候选编号位固定预留

`CandidateWindow::paint()` 此前只在活动行构造 `1. ` 前缀，非活动行的 `number` 为空串，`GetTextExtentPoint32W` 返回 0，文字直接从格子左边开始；`item_widths()` 也只在活动行按含编号的文字测量列宽。两者共同造成上下行文字错位。

修复：编号字符串在所有行都构造并测量，`item.left` 一律前移编号宽度，只有活动行调用 `TextOutW` 绘制编号；列宽测量也一律按含编号的文字进行。

## 四、标点后再输入的延迟

### 实测

Host 侧（真实 101,474 词词库，Release）：

- 空载往返 P95 199 微秒；
- 中文—标点—中文链路 P95 1281 微秒、最大 1540 微秒；
- 普通词逐键 P95 1463 微秒、最大 1701 微秒。

标点边界在 Host 上并不比普通按键慢，因此剩余延迟位于应用侧的 TSF 路径。词边界比词中多出的固有动作是 `StartComposition`，这是 TSF 语义要求的，无法省略。

### 本版去掉的浪费

候选窗每次 `show_at_anchor` 都会 `GetDC` 并对每个可见项调用 `GetTextExtentPoint32W`，而每个按键会定位两次：暂存快照一次、真实文本光标一次。窗口几何已锁定、候选内容未变化且可见行数未变化时，第二次定位只可能得出同一个矩形。现在这种情况下直接复用锁定矩形，跳过设备上下文与逐项测量。默认五行后每次测量最多 30 项，该项收益随行数增加。

`CandidateWindow` 新增 `layout_dirty_`：内容更新时置位，完成一次定位后清除，隐藏时置位。

## 五、提交之后第一个字母的候选栏延迟

### 定位

用户反馈「我，」之后打 `ta`、「你好吗？」之后打 `wo` 有卡顿。逐行核对 `CandidatePresenterModel`：

- 词中间：`reuse_caret = visible_ && caret_available_ && focused_session_ == session_id` 为真，`stage()` 保留光标，`CandidatePresenter::stage()` 随即 `show_at()`，候选栏在按键回复到达时就显示，与应用侧 TSF 编辑并行；
- 提交之后：`raw` 为空导致 `presenter_->hide()`，它清掉 `focused_session_` 与 `caret_available_`。下一键 `stage()` 的 `reuse_caret` 为假，`current_caret()` 返回 `nullptr`，`CandidatePresenter::stage()` 在 `return caret == nullptr || show_at(...)` 处直接返回，**不显示**。

候选栏因此要等 Shim 完成「`StartComposition` → `SetText` → `capture_composition_caret` 的 `GetTextExt` → caret 消息回到 Host」。`GetTextExt` 打在刚创建的组合上，宿主程序必须现排版，这在 Word、浏览器等程序中是几十毫秒量级。这也解释了为何该现象与标点无必然关系：任何提交之后都一样，只是句子里标点提交最频繁。

### 修复

`CandidatePresenterModel` 记住每个会话最后一次真实光标，`hide()` 不再丢弃它。下一次 `stage()` 若无法复用实时光标但存在同会话的记忆光标，则用它并置 `caret_inherited_`。`CandidatePresenter::show_at()` 对继承锚点调用新的 `CandidateWindow::show_at_provisional_caret()`，它以 `text_caret=false` 走 `show_at_anchor`，因而 `locked_to_text_caret_` 为假——第一个真实光标满足 `should_reanchor_candidate_window(true, false, true)`，允许校正一次，之后恢复既有的稳定锁。

定位算法本身未改：`place_candidate_window_at_text_caret` 与 `place_candidate_window` 是同一实现，差别只在锁定标记。

### 回归

`candidate_presenter_tests` 新增 `test_first_key_after_a_commit_reuses_the_previous_caret`：真实光标 → `hide()` → 下一词 `stage()` 必须给出非空光标且标记为继承、坐标等于上一词的真实光标、generation 为当前值；随后真实光标必须替换它并清除继承标记。

## 最终验证结果

- Windows x64 Release 全目标编译：通过；
- 完整 CTest：59/59 通过，0 失败；
- 分段取字前进、候选保留、`kady` 与 `buquan` 首选、长句验收：通过；
- 发布元数据与源码 SHA-256 校验：通过。

## 用户验证重点

1. `meilie` 按 `=` 后选「每」，候选应立即切到 `lie` 的字表；
2. 候选框展开后为五行（已有 `settings.ini` 写死 `visible_rows=3` 时需手工改为 5）；
3. 非活动行文字与编号行对齐；
4. 连续输入「中文符号中文符号中文」，观察标点之后是否跟手；
5. 「我，」之后打 `ta`、「你好吗？」之后打 `wo`，候选栏应立刻出现。
