# 英文候选词库来源

`data/english_lexicon.tsv` 由 `scripts/dev/build-english-lexicon.ps1` 合并三层生成，
三层的来源与许可各不相同，重新生成前务必先读本文件。

## 高频层（24,323 词）

项目原有的英文词频表，保留在 `data/english_lexicon.high-frequency.tsv`。
它的第二列是排名倒数而非真实词频。

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

## 重新生成

```powershell
python scripts/dev/export-wordfreq.py dicts/sources/wordfreq-en-200000/wordfreq-en.tsv 200000
pwsh ./scripts/dev/build-english-lexicon.ps1
```

调整 `-MinimumZipf` 可以收紧或放宽中频层：值越高噪音越少、覆盖越窄。
