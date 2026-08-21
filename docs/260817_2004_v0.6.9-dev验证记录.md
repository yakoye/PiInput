# PiInput v0.6.9-dev 验证记录

## 本版阻断问题

v0.6.6 宣称修复的两个问题在真机上都没有解决。用户提供两段录屏。

## 先确认版本，再谈现象

上一次发布出过「包里装的是旧二进制」的事故，所以这次先排除版本因素：

- 已安装 Shim `PiInputTSF.dll` SHA-256 前缀 `2B8E80B5`，与 v0.6.6 构建产物一致，文件时间 14:47:45；
- 已安装 Host `0.6.6-20260817-072018-24468`，SHA-256 前缀 `C7731B87`，与构建产物一致；
- Shim 是加载进每个应用进程的 DLL，换文件不会影响已在运行的进程，所以逐个核对了进程启动时间：Notepad++ (PID 10300) 15:20:43、Claude (PID 3364) 15:25:17，**都晚于 14:47:45**，且两者的模块列表中都确实加载了 `PiInputTSF.dll`；
- 两段录像分别是 15:24 和 15:27。

结论：两段录像都跑在真正的 v0.6.6 上，修复确实无效。

## 问题一：粘贴内容被删除

### 录像分析

30 fps 录像，抽到 20 fps 后裁出输入框那一行逐帧排列：

| 帧 | 输入框内容 |
| --- | --- |
| 1 | `之前基于旧包做的任何测试结论都不作数` |
| 2 | `之前基于旧包做的任何测试结论都不作数` |
| 3 | `f` |

**相邻两帧之间（50 毫秒）整段文字变成 `f`**。这是替换，不是先删除再输入。再抽一帧放大，`fd` 带下划线，确认是 TSF 组合而不是英文直输。

### v0.6.6 为什么修错

v0.6.6 判定根因是 `OnCompositionTerminated` 的无条件 `SetText(edit_cookie, 0U, L"", 0L)`。但按下第一个键时组合尚未建立，`mirror_.composition_text()` 为空，`range_holds_exactly` 遇到空 `expected` 直接返回 false，那段代码根本不会执行。**修改的位置不在事发路径上。**

这是同一个 bug 上的第二次误判，教训是：应当先用证据把路径钉死，再动代码。

### 真正的路径

`apply_composition_edit` 中：

```cpp
composition_->GetRange(&range);
range->SetText(edit_cookie, 0U, text.c_str(), text.size());
```

`SetText` 会覆盖整个范围。Claude、Codex 的输入框是 contenteditable，粘贴触发文档节点重建，此后 TSF 交回的组合区范围可以横跨用户刚粘贴的文字。

### 修复

新增 `range_text_equals()`（把原来的 `range_holds_exactly` 一般化，能正确处理「期望为空」这一情形，刚打开的组合正是这种），以及成员 `composition_written_` 记录上一次实际写入的内容，在组合对象被清空的每一处同步清空。

写入前校验：

```cpp
if (!range_text_equals(range, edit_cookie, composition_written_)) {
    // 结束这个被重新映射过的组合，不写入任何内容
    // 在当前选区重开一个，并确认新范围为空
    // 仍然不为空则放弃这次按键
}
```

取舍：丢掉一次按键远好于毁掉用户的内容。

`insert_text_at_selection()` 未改动。它只在「无组合且提交」时使用（英文直输、无组合的标点提交），本次录像走的不是这条路径；`InsertTextAtSelection` 替换非空选区在正常情况下是正确行为，无法与选区过时区分，因此不做推测性改动。

## 问题二：候选框位置偏移

### 录像分析

取到一帧同时能读到两个事实：

- 状态栏：`行: 19  列: 10`，第 19 行末尾正在组合 `hz`；
- 候选框绘制在第 14 行附近，候选内容是 `1.后 2.厚 3.後 4.侯 5.吼 6.鲔`。

候选内容与 `hz`（小鹤双拼 `hou`）完全对应，说明快照是当前的；位置却停在上一个词（第 13 行的「明天」）处，且整个词打完都没有移动。

### 根因：v0.6.6 引入的回归

一个词打开时 `stage()` 走「继承记住的锚点」分支，这是一个猜测。`CandidatePresenter::stage()` 随后调用 `show_at()` 绘制它，而 `show_at()` 会走 `model_.apply_caret()`，于是猜测执行了：

```cpp
anchor_locked_ = !current_.raw.empty();   // 猜测把锁设上了
```

一个往返之后真实光标到达，`apply_caret()` 命中 `if (anchor_locked_ && !current_.raw.empty())` 分支直接保持原位，**权威光标被丢弃**。候选框就停在猜错的位置直到该词结束。

`anchor_locked_` 的本意是消除词中间的横向漂移，结果把「先弹错、随后纠正」变成了「弹错就再也不纠正」，比 v0.6.5 更糟。

### 先复现再修

新增 `test_a_provisional_anchor_never_blocks_the_real_caret`，按录像时序构造：

1. 词一 `stage()` + 权威光标 (150,230)；
2. `hide()`（Host 在 `raw` 为空时确实调用 `presenter_->hide()`，已核对 `pipe_server.cpp`）；
3. 词二 `stage()`，继承到过时的 (150,230)，断言 `caret_is_inherited()`；
4. 绘制该临时锚点（`apply_caret`）；
5. 权威光标 (300,785) 到达，断言最终锚点 `top == 785`。

**改代码前第 5 步失败**，输出 `FAIL: the real caret corrects the guess instead of being ignored`，与录像一致。

期间还写过一版把「提交」建模成连续非空快照的测试，它同样失败，但核对 `pipe_server.cpp` 后确认真实 Host 在提交时走的是 `hide()`，该建模不成立，已改写为上面的真实时序。

### 修复

```cpp
const bool provisional = provisional_pending_;
provisional_pending_ = false;
...
anchor_locked_ = !current_.raw.empty() && !provisional;
```

`provisional_pending_` 由 `stage()` 的继承分支置位，被随后第一次 `apply_caret()` 消费，`hide()` 时清空。猜测照常绘制以保持响应速度，但不上锁，权威光标一定能纠正。

同时把锁的释放条件由「快照 `raw` 为空」改为「不是复用已显示的锚点」，防止锁跨词残留。

## 新增门禁

- `SetText` 覆盖组合区之前必须出现 `if (!range_text_equals(range, edit_cookie, composition_written_))`；
- 候选锚点上锁必须写成 `anchor_locked_ = !current_.raw.empty() && !provisional`。

## 最终验证结果

- Windows x64 Release 全目标编译：通过；
- 完整 CTest：通过，0 失败；
- 发布元数据与源码 SHA-256 校验：通过；
- 发布包：`PiInput-v0.6.9-dev-windows-x64.zip`。

## 用户验证重点

1. Claude 或 Codex 空输入框粘贴一段文字，**立刻**打字，确认粘贴内容仍在；
2. 满屏文本的 Notepad++ 随便点一个位置直接打字，确认候选框在插入光标下面；
3. 该词继续打长，确认候选框不漂移也不停在别的行；
4. **安装后请重启要测试的应用程序**。Shim 是加载进应用进程的 DLL，已经在运行的程序仍然使用内存中的旧版本。
