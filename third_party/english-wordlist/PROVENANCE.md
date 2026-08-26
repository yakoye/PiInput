# 英文候选词库来源

`data/english_lexicon.tsv` 由 `scripts/dev/build-english-lexicon.ps1` 合并三层生成，
三层的来源与许可各不相同，重新生成前务必先读本文件。

## 高频层（24,180 词）

项目原有的英文词频表，保留在 `data/english_lexicon.high-frequency.tsv`，
原表 24,323 词。它的第二列是排名倒数而非真实词频。

生成时会剔掉混进来的汉语拼音，三层合计约 463 词，高频层占 143 词，
判据见下方「拼音过滤」。

## 中频层（约 26,000 词）

来源：[wordfreq](https://github.com/rspeer/wordfreq)

- 取用日期：2026-08-26
- 取用方式：`wordfreq.top_n_list("en", 200000)` 与 `zipf_frequency`，导出为
  `dicts/sources/wordfreq-en-200000/wordfreq-en.tsv`
- 只保留 zipf ≥ 2.5、长度 3–14 的纯 ASCII 字母词
- **许可：代码 Apache-2.0（`LICENSE.wordfreq.txt`），数据 CC-BY-SA 4.0**
- **署名要求（CC-BY-SA 强制，不可省略）**：wordfreq 及其衍生数据必须署名
  SUBTLEX 作者，并保持"SUBTLEX 是可自由获取的数据"这一点清楚可见。
  wordfreq 自己拒绝提供 CSV 导出，理由正是"CSV 没有容纳署名和许可信息的
  位置，因此不符合 CC-BY-SA"——所以署名必须写在本文件与
  `THIRD_PARTY_NOTICES.md` 中，而不能只留在生成出的 TSV 里。
- 部分数据源自 Google Books Ngram Viewer，来源致谢见
  <http://books.google.com/ngrams>。

这一层是拼写辅助能不能用的关键。只有词形没有词频时，`palladium` 会被
`palladia`、`palladic` 这类同前缀的生僻词按词长挡在三个候选之外，而后两者
在词频表中根本不存在——正说明没人用它们。

## 低频层（约 200,000 词）

来源：[dwyl/english-words](https://github.com/dwyl/english-words)

- 文件：`words_alpha.txt`
- 取用日期：2026-08-26
- 原始 SHA-256：`3ed0c94610d8bcf7c11bbb49c56aa49c7234d32b66824df91f554169e572da48`
- 原始词条数：370,105；过滤后取长度 3–14 且未被上两层收录者
- **许可：Unlicense（公有领域，无任何附加义务）**

## 拼音过滤

wordfreq 统计的是真实英文文本，而汉语拼音在英文语境里出现得不少，于是
`hao`、`xie`、`dui`、`mei`、`zhong`、`shang` 这些都成了「英文词」，权重和
`car`、`cat` 落在同一段，靠分数分不开。在中文候选行里它们纯属噪音，
所以生成时直接剔除。

判据是两条同时成立：**是 `src/pinyin_syllables.inc` 里的合法音节，且
zipf < 4.0**。

后一条不能省。大量常用英文词本身就是合法拼音音节——`can`、`he`、`me`、
`man`、`men`、`run`、`sun`、`gun`、`fan`、`ban`、`pan`、`tan` 全是——
只按音节剔会把它们一并删掉，功能就废了。4.0 这个界是量出来的：该删的
最高是 `ni` 3.93，该留的最低是 `tan` 4.01，中间空着。

曾考虑改成更宽的「短词必须常用」（长度 ≤ 4 且 zipf < 4.0 就删），已否决：
那条规则会顺手删掉 `wane`(2.83) 和 `dow`(3.63) 这类货真价实的英文词，而
**这份词库英文模式也在用**，删了它们等于英文模式也打不出来。

因此 `ta`(4.02)、`le`(4.50)、`de`(5.23) 虽是音节但高于门槛，保留在词库里；
它们都是两个字母，而 `english_completion.cpp` 的 `kMinimumLength` 要求三个
字母起步，中文模式下取不到。同理 `nim`、`tam`、`wod`、`nid` 不是音节，
留在词库里，由中文侧的 `kChineseVetoesShortWords` 拦截。

## 重新生成

```powershell
python scripts/dev/export-wordfreq.py dicts/sources/wordfreq-en-200000/wordfreq-en.tsv 200000
pwsh ./scripts/dev/build-english-lexicon.ps1
```

调整 `-MinimumZipf` 可以收紧或放宽中频层：值越高噪音越少、覆盖越窄。
调整 `-UsageZipfFloor` 会改变拼音过滤的松紧，改动前请重跑
`piinput-english-completion`，那里有实测数据护着这条线。
