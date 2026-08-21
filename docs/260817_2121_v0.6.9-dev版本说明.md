# PiInput v0.6.9-dev 版本说明

v0.6.6 声称修复的两个问题**都没有修好**。用户重新录屏，两段录像都在真正的 v0.6.6 上（进程启动时间晚于 Shim 更新时间，确认加载的是新 DLL）。本版修的是真正的根因。

## 1. 粘贴的内容仍然被删除 —— v0.6.6 修错了路径

**录像证据**：Claude 输入框里粘贴的整段文字，在**一帧之内**（50 毫秒，20 fps 逐帧比对）变成第一个按键的 `f`。是**替换**，不是先删后打。放大帧可见 `fd` 带下划线，说明走的是中文组合路径。

**v0.6.6 错在哪**：当时判定根因是 `OnCompositionTerminated` 的无条件清空。但第一个按键按下时**还没有组合**，`mirror_.composition_text()` 为空，那条路径的 `range_holds_exactly` 直接返回 false，根本不会执行清空。修的地方不在事发路径上。

**真正的路径**在 `apply_composition_edit`：

```cpp
StartComposition(edit_cookie, selection.range, this, &composition_);
...
composition_->GetRange(&range);
range->SetText(edit_cookie, 0U, text.c_str(), text.size());   // ← 覆盖整个范围
```

Claude、Codex 的输入框都是 contenteditable。粘贴会让它们重建文档节点，此时 TSF 交回的组合区范围可以横跨用户刚粘贴的文字，`SetText` 就把它整段替换掉了。

**修复**：写入前先确认这个范围里装的**确实是上次自己写进去的内容**（刚打开的组合应当为空）。

```cpp
if (!range_text_equals(range, edit_cookie, composition_written_)) {
    // 丢掉这个被应用重新映射过的组合，在当前插入点重开一个干净的
    ...
}
```

新增成员 `composition_written_` 记录上一次实际写入的内容，在组合对象被清空的每一处同步清空。检测到不一致时：结束这个组合（**不写入**）、在当前选区重开一个、再确认新范围为空才写。如果重开后仍然不为空，就放弃这一次按键——丢一个按键远好于毁掉用户的内容。

## 2. 候选框位置仍然偏移 —— 而且是 v0.6.6 引入的回归

**录像证据**：状态栏显示插入光标在**第 19 行第 10 列**，候选框却画在**第 14 行**附近，差约 550 像素。候选内容是对的（`hz` → 后/厚/後），位置停在上一个词的地方，而且**整个词打完都不动**。

**根因**是 v0.6.6 新加的 `anchor_locked_`。一个词打开时，候选框先画在「记住的锚点」上（那是一个猜测，来自按键前探测，探测可能失败或过时）。问题在于绘制这个猜测时也走 `apply_caret`，于是**猜测把锁抢先设上**了：

```cpp
anchor_locked_ = !current_.raw.empty();   // 猜测也会上锁
```

一旦上锁，随后一个往返到达的**权威光标被直接忽略**，候选框就永久停在猜错的位置，直到这个词结束。v0.6.6 不但没修好定位，还把「先弹错、随后纠正」变成了「弹错就再也不纠正」。

**候选框不再画在任何猜测位置。** 用户反馈「总是差一步」说的正是这个：词打开时先画上一个词的锚点，随后才纠正。所有猜测手段（继承上一个词的光标、按键前探测）全部移除，候选框只在组合自身编辑会话里取得的真实光标处出现。代价是晚一个消息循环回合，换来位置永远正确——符合项目「候选准确率优先」的底线。

**同时撤销 v0.6.6 加的「按键前光标探测」。** 它是为修这个定位问题加的，但现在已经证明真正的缺陷是锚点锁——探测没有修好任何东西，却在**每个词的第一个按键上**插入一个同步 TSF 编辑会话加一次阻塞的 IPC 往返。这是热路径上没有收益的风险，予以移除，源码门禁恢复为「Shim 的按键路径上禁止任何只读编辑会话」。

**修复**：只有权威光标可以上锁。

```cpp
const bool provisional = provisional_pending_;
provisional_pending_ = false;
...
anchor_locked_ = !current_.raw.empty() && !provisional;
```

猜测照样绘制（候选框保持即时响应），但绝不上锁；一个往返之后真实光标一定能纠正它。同时把锁的释放条件从「快照 raw 为空」改成「不是复用已显示的锚点」，避免锁跨词残留。

探测命中时两者位置一致，看不到跳动；探测失败时最多一帧后归位，而不是错一整个词。

## 验证

- 先写测试复现，再改代码。`test_a_provisional_anchor_never_blocks_the_real_caret` 按录像的真实时序构造：上一个词锚在 (150,230)，提交 → `hide()`，下一个词开在过时的临时锚点，随后权威光标报 (300,785)。**改代码前该测试失败**，断言真实光标被忽略；改后通过；
- 完整 CTest 全绿；
- 新增两条源码门禁：`SetText` 前必须用 `composition_written_` 校验组合区范围；候选锚点只能由权威光标上锁。

## 用户验证重点

1. Claude 或 Codex 空输入框粘贴一段文字，**立刻**打字，确认粘贴内容还在；
2. 满屏文本的 Notepad++ 里随便点一个位置直接打字，确认候选框就在插入光标下面；
3. 把那个词继续打长，确认候选框不横向漂移，也不停在别的行。

如果第 1 条仍然复现，说明覆盖发生在别处——本版的防护只会**拒绝**写入不属于自己的范围，不会自己删字，那就需要另外定位。

## 3. ChatGPT / Codex 里根本切不到 PiInput

用户报告在 ChatGPT 中按 Win+空格或 Ctrl+Shift 都切不到 PiInput。逐进程核对模块列表：**ChatGPT 的所有进程都没有加载 `PiInputTSF.dll`**，而 Claude、Notepad++、Notepad4 都加载了；四个 ChatGPT 进程都加载了 TSF 核心 `MSCTF.dll`，说明 TSF 本身是通的。

`GetProcessMitigationPolicy` 查得 `SignaturePolicy=0x0`、`ImageLoadPolicy=0x0`，排除「仅允许微软签名 DLL」这类拦截。

真正原因在进程映像路径：它位于 `Program Files\WindowsApps\OpenAI.Codex_26.810.7004.0_x64__2p2nqsd0c76g0pp\ChatGPT.exe`，是 **MSIX 打包应用，运行在 AppContainer 沙箱内**。AppContainer 进程只能读取授予了 `ALL APPLICATION PACKAGES`（SID `S-1-15-2-1`）的文件。核对 ACL：

| 文件 | ALL APPLICATION PACKAGES |
| --- | --- |
| `PiInputTSF.dll`（Runtime/Shim 下） | **缺失** |
| `System32\SogouTSF.ime` | ReadAndExecute |

所以 Windows 无法把 Shim 载入 ChatGPT，输入法在那里不可用，切换列表自然跳过它。

修复：安装器新增 `grant_app_container_read()`，用 `SetEntriesInAclW` / `SetNamedSecurityInfoW` 给 Shim 目录与 DLL 授予该组的读取+执行权限，并且在「文件字节未变」的路径上也执行一次，让已有安装靠重装即可修复权限。新增源码门禁禁止这段逻辑被删除。

## 4. 尚未解决：Claude 中字母偶尔以普通文本落下

用户另外提供两段录像（19:31 ChatGPT、19:33 Claude，均在 v0.6.6 上）：输入拼音字母时，字母**不带下划线**地直接落进输入框，没有组合区也没有候选，偶尔才会正常成词。

**这个问题本版没有定位到根因，也没有针对性修复。** 已经排除的：

- 不是断线。Host 往返失败时 `dispatch_now` 返回 false 而按键仍被吞掉，字母会消失而不是留下；
- 不是候选框位置。整帧内没有任何候选栏，且文字没有组合下划线，说明根本没有建立组合。

本版对它做的唯一处置是移除按键前光标探测（见上），因为那是 v0.6.6 在按键热路径上唯一的新增行为，且已确认没有收益。**这是降低风险，不是已证实的修复。**

如果升级后仍然复现，请提供：出问题时任务栏输入法图标显示的是「中」还是「英」；是否在此之前按过 Shift；以及是否在同一个应用里换过输入框。这三点能把范围缩到「Shim 与 Host 的输入模式是否同步」这一条上。

## 5. 候选框定位：用独立工具验证后才改源码

新增 `piinput-caret-demo.exe`：它用**生产代码里同一个 `CandidateWindow` 类**，光标不走输入法链路而是用 `GetGUIThreadInfo` 直接从当前程序读，把「光标从哪来」和「什么时候允许动」两个变量拆开，并把每次的坐标写进 CSV 日志。

第一份日志（20 个采样）的结论：候选栏四个坐标 `271,1088,885,1138` **一次没变**，而光标跨了 5 行、横移 500 多像素。根因是窗口层的第二道锁：

```cpp
return !geometry_locked || (!locked_to_text_caret && incoming_text_caret);
```

一旦锁定到真实光标，后续任何光标都无法让窗口重新定位，直到 `hide()`。

第二份日志（44 个采样，覆盖 `caret_l` 175~703、`caret_t` 462~1191、同行换位与同列换行、125% 缩放）：**全部 `dx=0`、`dy_below_caret=5`**。定位数学本身没有任何问题，问题完全在释放策略。

据此改源码，规则按用户给的语义：

- `CandidateWindow::release_anchor()`：不隐藏窗口即可释放几何锁；
- `CandidatePresenterModel::word_just_opened()`：区分「打开新词」与「继续当前词」；
- `CandidatePresenter::stage()` 在打开新词时显式释放锚点。

即**组合打开时按真实光标取锚点，打字期间冻结不漂移，上屏后释放**。此前锁只由 `hide()` 的副作用释放，这种隐式耦合正是候选框停在上一个词位置的原因。

新增门禁：打开新词必须显式调用 `release_anchor()`。

## 6. 可选的候选框定位诊断

`CandidatePresenter::show_at()` 新增一段默认关闭的记录。存在 `%TEMP%\piinput-caret-trace.on` 时，Host 会把每一次候选框定位写入 `%TEMP%\piinput-caret-trace.csv`：

```text
tick_ms,session,generation,word_opens,has_caret,
caret_l,caret_t,caret_r,caret_b,bar_l,bar_t,bar_r,bar_b
```

`caret_*` 是 Shim 在组合编辑会话里取得、经协议送到 Host 的光标矩形，`bar_*` 是候选窗的实际落点，`word_opens` 标明这一次是否为新词开头。

只记录几何坐标，**不记录按键、不记录文字、不记录组合内容**，符合「不保存完整按键记录和完整输入历史」的产品底线。用标记文件而不是环境变量，是因为 Host 通常由 Shim 在别的应用进程里拉起，只会继承那个应用的环境。

`tick_ms` 取自 `GetTickCount`，与进程外用 `GetGUIThreadInfo` 采集真实插入点时的时钟一致，因此两路数据可以按时间对齐，用来判断 `GetTextExt` 返回的矩形与真实插入点之间的偏差。

首批实测（10 次定位）已经显示：`bar_l` 与 `caret_l` **每次完全相等**，`bar_t` 恰好等于 `caret_b + 5`。定位链路本身无误；同时发现部分样本的 `caret_r - caret_l` 为 121 像素——那是整段组合文字的宽度而非 1~2 像素的插入点，说明 `GetTextExt` 在 Scintilla 上有时返回整个组合区范围。这是后续排查的方向。
