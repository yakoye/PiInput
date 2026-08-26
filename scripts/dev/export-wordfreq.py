"""把 wordfreq 的英语词频导出成 TSV，供词库构建脚本使用。

只导出 word<TAB>zipf 两列。zipf 是 wordfreq 的对数词频刻度，越大越常用
（the 约 7.6，palladium 约 2.6），刚好可以直接当排序键用。
"""
import sys

from wordfreq import top_n_list, zipf_frequency

OUT = sys.argv[1] if len(sys.argv) > 1 else "wordfreq-en.tsv"
LIMIT = int(sys.argv[2]) if len(sys.argv) > 2 else 200000

rows = []
for word in top_n_list("en", LIMIT):
    if not word.isalpha() or not word.isascii():
        continue
    rows.append((word.lower(), zipf_frequency(word, "en")))

# 去重后按 zipf 降序，保证输出稳定
seen = {}
for word, zipf in rows:
    if word not in seen or zipf > seen[word]:
        seen[word] = zipf

with open(OUT, "w", encoding="utf-8", newline="\n") as handle:
    for word, zipf in sorted(seen.items(), key=lambda item: (-item[1], item[0])):
        handle.write(f"{word}\t{zipf:.4f}\n")

print(f"wrote {len(seen)} words to {OUT}")
