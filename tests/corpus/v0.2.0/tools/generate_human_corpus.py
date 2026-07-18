#!/usr/bin/env python3
"""Generate Markdown and plain-text human test corpora for v0.2.0.
生成 v0.2.0 人工测试 Markdown 与纯文本语料。
"""
from __future__ import annotations
import json
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]

def load(rel): return json.loads((ROOT / rel).read_text(encoding="utf-8"))

def main() -> int:
    xh_words = load("tests/xiaohe_test_cases.json")
    long_cases = load("corpus/long_sentences.json")
    ctx = load("corpus/context_disambiguation.json")
    seg = load("corpus/segmentation_ambiguity.json")
    prof = load("corpus/professional_terms.json")
    fuzzy = load("corpus/fuzzy_pinyin.json")
    corr = load("tests/correction_test_cases.json")
    scheme = load("schemes/xiaohe.json")

    md = ["# 中文输入法测试语料 v0.2.0", "", "本版重点验证长句切分、上下文消歧、专业词库、容错纠错、模糊音和用户词频学习。", "", "## 1. 小鹤双拼基础词", "", "| 目标文本 | 拼音音节 | 双拼分码 | 连续输入 |", "|---|---|---|---|"]
    for item in xh_words[:30]:
        md.append(f"| {item['target_text']} | `{' '.join(item['pinyin_syllables'])}` | `{' '.join(item['syllable_codes'])}` | `{item['input_sequence']}` |")
    md += ["", "## 2. 长句切分与候选质量", ""]
    for item in long_cases:
        md += [f"### {item['id']}", "", f"目标：{item['target']}", "", f"全拼：`{item['full_pinyin_continuous']}`", "", f"音节：`{' '.join(item['pinyin_syllables'])}`", "", f"测试点：{'、'.join(item.get('test_points', []))}", ""]
    md += ["## 3. 上下文消歧对照组", "", "同一组必须连续测试，比较上下文改变后目标词的排名。", "", "| 用例 | 歧义编码 | 目标词 | 对照词 | 目标句 |", "|---|---|---|---|---|"]
    for item in ctx:
        md.append(f"| {item['id']} | `{item['ambiguous_input']}` | {item['target_word']} | {item['contrast_word']} | {item['target_sentence']} |")
    md += ["", "## 4. 切分歧义", "", "| 输入 | 目标 | 强制切分 | 上下文 |", "|---|---|---|---|"]
    for item in seg:
        md.append(f"| `{item['input']}` | {item['target']} | `{item.get('forced_input') or '-'}` | {item['context']} |")
    md += ["", "## 5. 专业词库", ""]
    domains = {}
    for item in prof: domains.setdefault(item['domain'], []).append(item)
    for domain, items in domains.items():
        md += [f"### {domain}", ""]
        for item in items:
            md.append(f"- `{item['full_pinyin_continuous']}` → {item['text']}")
        md.append("")
    md += ["## 6. 容错纠错抽样", "", "完整 160 条结构化用例见 `tests/correction_test_cases.json`。关闭纠错时要求行为稳定；开启纠错时记录目标候选排名，不强制所有错码都必须首选。", "", "| 方案 | 类型 | 正确输入 | 错误输入 | 目标 |", "|---|---|---|---|---|"]
    for item in corr[:32]:
        md.append(f"| {item['scheme']} | {item['mutation_type']} | `{item['source_input']}` | `{item['mutated_input']}` | {item['target_text']} |")
    md += ["", "## 7. 模糊音开关", "", "| 模糊音 | 状态 | 测试输入 | 正确输入 | 目标 |", "|---|---|---|---|---|"]
    for item in fuzzy:
        md.append(f"| {item['pair']} | {'开' if item['fuzzy_enabled'] else '关'} | `{item['test_input']}` | `{item['correct_input']}` | {item['target']} |")
    md += ["", "## 8. 用户学习", "", "按 `tests/user_learning_test_cases.json` 执行：记录初始排名、重复选择、重启、删除用户词、恢复默认、密码框与隐私模式。", "", "## 9. 字符与混合输入", "", "```text", "中文 English 混合输入 2026。", "PCIe 6.0、GPU、CPU 和 AI 芯片。", "yakoye.github.io", "user@example.com", "C:\\Users\\color\\Documents", "/Users/name/Documents", "🙂 😂 👍 🚀 ❤️ 👍🏻 👨‍👩‍👧‍👦", "龘 鱻 麤 靐 齉 爨 彧 翀 昉 玥 𠮷 𠀀", "```", "", "## 10. 非打印按键", "", "请使用 `keyboard/manual_key_checklist.md` 和状态机 JSON；静态文本无法验证 Backspace、Delete、方向键、Esc、Enter 和修饰键。"]
    (ROOT / "generated/Chinese_IME_Test_Corpus_v0.2.0.md").write_text("\n".join(md) + "\n", encoding="utf-8")

    txt = ["中文输入法测试文本 v0.2.0", "", "【长句切分】"]
    for item in long_cases:
        txt += [item['target'], item['full_pinyin_continuous']]
    txt += ["", "【上下文消歧】"]
    for item in ctx:
        txt += [item['target_sentence'], item['full_pinyin_continuous']]
    txt += ["", "【专业词库】"]
    for item in prof:
        txt.append(f"{item['text']}  {item['full_pinyin_continuous']}")
    txt += ["", "【模糊音】"]
    for item in fuzzy:
        txt.append(f"{item['pair']} {'ON' if item['fuzzy_enabled'] else 'OFF'} {item['test_input']} -> {item['target']}")
    txt += ["", "【字符、数字和混合输入】", "0123456789", "abcdefghijklmnopqrstuvwxyz", "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "`1234567890-=", "~!@#$%^&*()_+", "[]\\;',./", "{}|:\"<>?", "中文 English 混合输入 2026。", "PCIe 6.0、GPU、CPU 和 AI 芯片。", "🙂 😂 👍 🚀 ❤️ 👍🏻 👨‍👩‍👧‍👦", "龘 鱻 麤 靐 齉 爨 彧 翀 昉 玥 𠮷 𠀀"]
    (ROOT / "generated/Chinese_IME_Test_Corpus_v0.2.0.txt").write_text("\n".join(txt) + "\n", encoding="utf-8")
    print("generated human corpus v0.2.0")
    return 0
if __name__ == "__main__":
    raise SystemExit(main())
