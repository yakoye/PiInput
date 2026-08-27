# 英文候选词库来源

`data/english_lexicon.tsv` 由 `scripts/dev/build-english-lexicon.ps1` 合并三层生成，
三层的来源与许可各不相同，重新生成前务必先读本文件。

## 高频层（28,670 词）

`data/english_lexicon.high-frequency.tsv`。第二列是排名倒数而非真实词频，
`the=24,323` 最常用，依次递减。

这一层由两份来源合并而成，**构建脚本会把合并结果写回该文件**，因此「高频词汇」
始终只有一份数据，而不是一张原表加一张待合并的清单：

| 来源 | 词数 | 说明 |
| --- | --- | --- |
| 项目原有词频表 | 24,188 | 原 24,323 词，剔除混入的拼音后剩余 |
| `dicts/english_common_words_10000plus.txt` | 4,482 | 原表没有的部分，按各自排名插入 |

排名对齐：原表 `the=24,323` 递减，常用表 `the=1` 递增，补入时按
`24,324 - 常用表排名` 翻转，使同等常用度的词落在同一位置。

重跑稳定：补入的词已在文件里，第二遍直接跳过，不会再分配排名。两个输出文件
都从同一份排序结果写出，行序只取决于内容，SHA256 因此可复现。

### 常用标记

合并进来的这份表还有第二个用途：凡它收录的词，词条第三列会带上
`EnglishCandidateFlag::common`（64）。**三字母输入是否给出英文候选只看这个
标记，不看权重。**

用成员判定而不是权重阈值，是因为任何一条阈线都会在中间切出空集，把 `dog`、
`egg` 这类日常词误伤——实测取 1,020,000 时正是如此。这份表本身就是「最常用的
词」，判据和问题是同一件事，不需要再挑一个数。

实测分离干净：`dog`(688)、`egg`(1243)、`cat`(1595)、`bus`(1310)、`car`(266)
全部收录，而 `tam`、`nim`、`nid`、`wod`、`ken`、`niz`、`tzn`、`jiy`、`wom`
一个都不在。

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

判据是三条：**是 `src/pinyin_syllables.inc` 里的合法音节，zipf < 4.0，
且不在常用词表里**。

第三条是后加的，不能省。只按前两条会误删 `nun`、`wan`、`hen`、`shun`、`chi`、
`fang`、`pang`、`gong`——全是货真价实的英文词，只因为同时是拼音音节、zipf 又
低于 4.0。zipf 阈值本来就是在替「这是不是一个英语词」做判断，而常用词表是人工
整理的答案，比阈值权威。

放行不会把噪音带回来：真正的拼音混入词 `hao`、`xie`、`dui`、`mei`、`shi`、
`wo`、`ni`、`bu`、`zhong`、`shang` 一个都不在常用表里。唯一重叠的是 `ta`
（排名 11,056），而它只有两个字母，`kMinimumLength` 要求三个字母起步，中文
模式下取不到。

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
