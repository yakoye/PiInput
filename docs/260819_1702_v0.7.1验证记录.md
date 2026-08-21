# PiInput v0.7.1 验证记录

## 修复依据

- 候选越界：`Engine::query` 的补全分支直接把 `query_prefix` 的结果全部收下，词条能有多少音节就收多少。小鹤 `bjg` 只解析出 `ban` 加半个 `g`，却匹配到六个音节的 `ban'gong'shi'zi'dong'hua`。补全跨度现在被限制在 `complete_syllables.size() + 1`。
- 单字母候选偏窄：`DevLexicon::query_prefix` 按 `scan_limit` 扫描已排序的拼音键。前缀 `b` 下 `ba'*` 的多音节词条就把 4096 的预算耗尽，扫描永远走不到 `bu`。补全改为遍历 `PinyinSegmenter::standard_syllables()` 里以该前缀开头的音节做精确查询，有界前缀扫描保留为补充，覆盖词库里的非标准音节。
- 结尾半个拼音卡死：`SegmentSelection::complete()` 要求 `trailing_prefix` 为空，`Engine::query_segment` 在 `syllable_offset >= syllables.size()` 时直接返回空。两者叠加使得尾部未完成音节既查不到候选、也不能被消费，`finish()` 永远返回空串。尾部音节现在是一个正常的分段，由补全解析为恰好一个音节后消费。
- 首次 `3.` 变成 `3。`：`OnKeyDown` 对当前上下文做惰性绑定时会走 `bind_context`，后者清掉 `last_passthrough_was_digit_`。惰性绑定是接到用户正在输入的上下文，不是切换文档，现在跨这次绑定保留该标记。

## 自动验证

- Windows x64 Release 全目标构建：通过；
- 完整 CTest：59/59 通过，0 失败，总耗时 63.18 秒；
- `piinput-segment-selection`：通过，新增三条回归——尾部未完成音节可被补全并消费、补全跨度不超过已输入音节数、`woxmzdb` 选中「我现在」后仍能继续解析 `b` 并一次提交「我现在不」；
- `piinput-incremental-decoder`：通过，含 `prefix_scan_limit = 0` 仍然完全关闭补全的契约；
- 三个长文本真实协议回放 `piinput-typing-latency`：通过；
- `piinput-keystroke-latency`、`piinput-external-incremental-performance`、`piinput-external-long-full-performance`：通过；
- `piinput-corpus-regression`、`piinput-character-coverage`：通过；
- 发布版本、文件清单与源码 SHA-256 门禁：通过；
- `dist/windows-x64` 已由当前 Release 构建重新生成。

## 候选质量实测

用发布词库 `dicts/cache/piinput-base.lex`（891,195 条）通过 `piinput-cli --schema flypy` 复核：

```text
bjg      办公  半个  办个  办过  半干  颁给  搬过  半高
bhg      帮个  帮工  帮过  帮规  帮哥  帮贡  邦国  绑个
b        把  被  不  本  边  吧  白  别
w        我  为  无  问  外  王  位  文
wovidcl  我知道了  我知道咯  我知道喽  我知道啦  我知道  我只  我知  我
woxmzdb  我现在  我先  我县  我  握  窝  卧  沃
```

修复前 `bjg` 首屏是「办公室自动化 办公自动化 半个多世纪」，`bhg` 首位是「棒骨土豆地饵汤」，`b` 只能给出 `ba` 一个音节的字，`wovidcl` 首位是「我知道了哦」。

## 用户真机验证

以下依赖 Shim 在真实应用里的按键顺序，自动回放覆盖不到，需要在 Notepad++ 等应用中确认：

- 小鹤 `woxmzdb`：空格取「我现在」，候选栏给出「不」等单字，再按空格上屏「我现在不」；
- 小鹤 `wovidcl`：空格取「我知道」，再取「了」，上屏「我知道了」；
- 新开窗口后第一次输入 `3.`，应得到 `3.` 而不是 `3。`；
- `0.7.1`、`1.2.3` 保持 ASCII 点；
- 安装后关闭并重新打开测试应用，确保加载 v0.7.1 Shim。

## 已知边界

`3..` 无法得到两个 ASCII 点，仍然得到 `3.。`。第一个点按小数点处理，第二个点回到中文句号，否则数字后面就再也打不出 `。`。用户已确认保留当前取舍。
