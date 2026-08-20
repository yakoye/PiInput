# PiInput v0.6.2-dev 验证记录

## 本版阻断问题

1. 按 Shift 切到英文后打字特别慢；
2. 按 `=` 之后原来的整词候选整行消失，被逐字候选顶替。

## 一、英文直输

### 定位

英文候选默认关闭（`EnglishSettings::enabled` 为 false），此时 `HostSession::apply` 走 `mode_ == english && english_ == nullptr` 分支，对每个字母返回 `HostAction::commit`。

先排除 Host：新增 `piinput-host-client-fixture --english-burst`，切到英文后连打 300 键，Host 侧逐键 P50 为 181 微秒、P95 为 408 微秒、最大 731 微秒，与中文同级，说明瓶颈不在引擎或管道。

再看 Shim：`apply_composition_edit` 在没有进行中组合时，对提交执行 `StartComposition` → `SetText` → `EndComposition` → `SetSelection`。也就是每个英文字母都让宿主程序完整经历一轮输入法状态切换，四步各自跨边界通知。

### 修复

提交到达且没有进行中组合时，改用 `ITfInsertAtSelection::InsertTextAtSelection` 直接在插入点写入，再把光标折叠到文本末尾。一次操作取代四次。组合区为空时单独输入的中文标点走同一条路径。宿主拒绝移动自身光标不再被当作提交失败，因为文本已经落到文档里。

### 门禁

Windows 源码回归新增：没有进行中组合的提交必须走 `insert_text_at_selection` / `ITfInsertAtSelection`，不得再创建并结束 TSF 组合。

## 二、按 `=` 保留整词候选

### 定位

`ImeSession::refresh_segment()` 只保留 `normal_browse_candidate_count` 条整词，而该计数是「候选表开头连续可信条目」的长度：只统计 `exact_lexicon`、`prefix_lexicon`、`user_phrase` 和单词单字的 `incomplete_completion`，遇到第一条句子解码结果就停止。

`kady`（ka'dun）第一行是六条真实的两字组合，但只有开头一条算可信命中，计数为 1；`fwihkk`（非常快）第一行全是句子解码结果，计数为 0。于是按 `=` 之后整行被逐字候选顶替。用户截图中的第二张就是这个现象。

### 修复

`enter_segment_selection()` 接受「用户当前已经看到的整词条数」，由 `HostSession` 按 `(当前行 + 1) × 每行条数` 传入；`refresh_segment()` 保留可信计数与该值中的较大者。分段取字始终追加在已见词行下方，长词段优先、单字兜底的既有顺序不变。

真实词库下 `kady` 按一次 `=`：

```text
第 1 行：卡吨 卡顿 卡盾 卡蹾 卡炖 卡蹲
第 2 行：卡 喀 咖 咔 佧 咯
第 3 行：垰 胩 鉲 擖 裃
```

### 回归

- `host_session_tests` 新增 `kady` 场景：第一行整词必须原样保留，逐字候选追加在下方，激活项落在追加段的第一个未定音节上；
- `fwihkk` 原有断言此前锁定的是被顶替后的列表（首项 `非场`）。该断言描述的是缺陷行为，现改为校验 `非常快` 整行保留、追加段以真实长词 `非常` 开头；
- 新增 `piinput-host-client-fixture --expand <输入> <按=次数>`，可直接转储真实词库下的候选分行结果。

## 最终验证结果

- Windows x64 Release 全目标编译：通过；
- 完整 CTest：全部通过，0 失败；
- 三个长文本逐键回放门禁：通过；
- 发布元数据与源码 SHA-256 校验：通过。

## 用户验证重点

1. 按 Shift 切到英文，连续输入 `the quick brown fox jumps over a lazy dog`，观察是否还有迟滞；
2. 输入 `kady` 后按一次 `=`，第一行整词候选应保留。
