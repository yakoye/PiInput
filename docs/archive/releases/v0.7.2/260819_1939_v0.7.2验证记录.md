# PiInput v0.7.2 验证记录

## 修复依据

- 整串打完却只能上屏前两个音节：`Engine::query` 只对首选 parse 的每个前缀长度做精确查询，词库里没有覆盖全部音节的条目时就没有任何全覆盖候选。v0.7.1 停用 `IncrementalDecoder` 之后，这条路径完全没有替代品。现在在主 parse 上跑一次有界动态规划，把真实多字词拼接成覆盖全部音节的候选。
- 全拼结尾带半个音节漏词：`src/pinyin_prefix.cpp` 的 `sort_and_limit` 第一优先级是「完整音节数降序」，第二优先级是「未解析字母数降序」，两个方向都错。`woxianzaib` 因此选中 `wo'xi'an'zai` 而不是 `wo'xian'zai`，`Engine::query` 只用首选 parse 查词，「我现在」永远查不到。`PinyinSegmenter::segment` 本身已经把「偏好更少更长的音节」算进了评分，排序被原始音节数覆盖了。

## 修复方式

- 拼接使用动态规划，跨度上限 8 个音节，每个跨度取词库前 4 条，每个位置保留 4 条路径，最多 4 个词，输出最多 2 条。每一段都必须是真实的多字词条，单字不得作为衔接。
- 拼接排序：词数少的优先，然后是最弱的那一环的词频，再然后是总词频。`EngineCandidate::score` 带上总词频，否则外层排序在 `base_weight` 打平后退化成字典序。
- `Engine::query` 的排序新增「词数少者优先」判据，保证单条真实词库条目压过同样覆盖的拼接候选。
- 前缀展开排序改为：未解析字母数升序 → 分词器评分降序 → 音节数升序 → 字典序定序。

## 自动验证

- Windows x64 Release 全目标构建：通过；
- 完整 CTest：59/59 通过，0 失败，总耗时 60.21 秒；
- `piinput-segment-selection`：通过，新增两条回归——真实多字词拼接覆盖全部输入且排序正确、全是单字的输入不得被拼成句子；
- `piinput-incremental-decoder`：通过，新增一条回归锁住前缀展开的排序契约（`xianb` 不得拆成 `xi'an`，`woxianzaib` 首选必须是 `wo'xian'zai'b`）；
- `piinput-core-tests`、`piinput-lexicon-query`、`piinput-external-dictionary-regression`：通过，三处「不拼句」断言已更新为新契约（允许拼接真实多字词，禁止单字衔接，真实整词优先）；
- `piinput-typing-latency` 3.36 秒、`piinput-keystroke-latency` 1.68 秒、`piinput-external-incremental-performance` 2.23 秒、`piinput-external-long-full-performance` 1.65 秒：全部通过，拼接没有突破按键延迟门禁；
- 发布版本、文件清单与源码 SHA-256 门禁：通过；
- `dist/windows-x64` 已由当前 Release 构建重新生成。

## 候选质量实测

用发布词库 `dicts/cache/piinput-base.lex`（891,195 条）通过 `piinput-cli` 复核。

小鹤双拼，整覆盖拼接：

```text
bwffyshu     备份用户  备份拥护  备份  辈分  悲愤  被分
xpzduuru     卸载输入  写在输入  卸载  写在  些    血
dnyslmffxi   调用链分析  调用链  调用  掉    调    屌
```

修复前这三条首位分别是「备份」「卸载」「调用」，都只覆盖前两个音节。

全拼，结尾半个音节：

```text
woxianzaib   我现在  我先  我县  我        （修复前：我系 我洗 我喜 我）
nihaob       你好    拟好  你    尼        （修复前：你 尼 泥 逆）
xihuanb      喜欢表  喜欢  西环  洗换      （修复前：西湖 洗护 潟湖 稀糊）
zhongguor    中国人  中国日 中国热 中国    （修复前：无候选）
beijingd     北京的  背景的 北京东 背景灯  （修复前：无候选）
```

真实整词压过拼接：

```text
zhonghuarenmin   中华人民  种花人民  种花人  中华
yifanfengshun    一帆风顺  一翻丰顺  一番丰顺  一翻
shishiqiushi     实事求是  事实求师  实施求师  事实
zhongguorenmin   中国人民  种过人民  中国人  中国
```

v0.7.1 的修复逐条复核，未回归：

```text
woxmzdb   我现在  我先  我县  我
wovidcl   我知道了  我知道咯  我知道喽  我知道啦
bjg       办公  半个  办个  办过
bhg       帮个  帮工  帮过  帮规
b         把  被  不  本
w         我  为  无  问
```

## 用户真机验证

以下依赖 Shim 在真实应用里的按键顺序，自动回放覆盖不到，需要在 Notepad++ 等应用中确认：

- 小鹤 `bwffyshu`、`xpzduuru`、`dnyslmffxi` 首位是整句，一次空格上屏；
- 全拼 `nihaob`、`zhongguor`、`beijingd` 首位是正确的词；
- 小鹤 `woxmzdb` 两次空格上屏「我现在不」；
- 新开窗口后第一次输入 `3.`，应得到 `3.` 而不是 `3。`；
- 安装后关闭并重新打开测试应用，确保加载 v0.7.2 Shim。

## 已知边界

- `3..` 无法得到两个 ASCII 点，仍然得到 `3.。`。第二个点回到中文句号，否则数字后面就再也打不出 `。`。用户已确认保留当前取舍。
- 组合区显示原始字母串，不按音节插入 `'` 分隔。用户确认这一项优先级不高，本版不处理。
