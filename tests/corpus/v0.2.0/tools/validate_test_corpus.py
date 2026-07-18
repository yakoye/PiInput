#!/usr/bin/env python3
"""Validate v0.2.0 JSON, IDs, pinyin inventory, Xiaohe codes and derived cases.
校验 v0.2.0 JSON、用例 ID、拼音音节、小鹤编码和派生测试。
"""
from __future__ import annotations
import json
import sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]

def load(path):
    try: return json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc: raise RuntimeError(f"failed to parse {path}: {exc}") from exc

def split_initial_final(syllable: str):
    s = syllable.lower().replace("ü", "v")
    for ini in ("zh","ch","sh"):
        if s.startswith(ini): return ini, s[len(ini):]
    if s and s[0] in "bpmfdtnlgkhjqxrzcsyw": return s[0], s[1:]
    return "", s

def encode(syllable, scheme):
    s = syllable.lower().replace("ü", "v")
    if s in scheme["zero_initial_map"]: return scheme["zero_initial_map"][s]
    ini, fin = split_initial_final(s)
    return scheme["initial_map"][ini] + scheme["final_map"][fin]

def main() -> int:
    errors = []
    for path in ROOT.rglob("*.json"):
        try: load(path)
        except RuntimeError as exc: errors.append(str(exc))
    full = load(ROOT / "schemes/full_pinyin.json")
    scheme = load(ROOT / "schemes/xiaohe.json")
    inventory = set(sum(full["syllables_by_group"].values(), []))
    for syl in sorted(inventory):
        try:
            if len(encode(syl, scheme)) != 2: errors.append(f"{syl}: Xiaohe code length is not 2")
        except Exception as exc: errors.append(f"cannot encode {syl}: {exc}")
    for rel in ["corpus/long_sentences.json", "corpus/context_disambiguation.json", "corpus/professional_terms.json", "corpus/correction_seeds.json"]:
        for item in load(ROOT / rel):
            for syl in item.get("pinyin_syllables", []):
                if syl.replace("ü","v") not in inventory: errors.append(f"{rel}/{item.get('id')}: unknown pinyin syllable {syl}")
    seen = set()
    test_files = sorted((ROOT / "tests").glob("*.json"))
    test_count = 0
    for path in test_files:
        data = load(path)
        if not isinstance(data, list):
            errors.append(f"{path.name}: expected a JSON list")
            continue
        for item in data:
            cid = item.get("id")
            if not cid: errors.append(f"{path.name}: missing id")
            elif cid in seen: errors.append(f"duplicate test id: {cid}")
            else: seen.add(cid)
            test_count += 1
    corr = load(ROOT / "tests/correction_test_cases.json")
    if len(corr) != 160: errors.append(f"expected 160 correction cases, got {len(corr)}")
    for item in corr:
        if item["source_input"] == item["mutated_input"]: errors.append(f"{item['id']}: mutation did not change input")
        if item.get("edit_distance_expected") != 1: errors.append(f"{item['id']}: expected edit distance must be 1")
    fuzzy = load(ROOT / "tests/fuzzy_pinyin_test_cases.json")
    if len(fuzzy) != 20: errors.append(f"expected 20 fuzzy cases, got {len(fuzzy)}")
    required = [
        "README.md", "docs/PROJECT_OVERVIEW.md", "docs/TEST_PLAN.md", "docs/ROADMAP.md",
        "docs/DEVELOPMENT_CONSTRAINTS.md", "docs/METRICS_AND_REPORT_FORMAT.md",
        "docs/VERSION_NOTES_v0.1.0.md", "docs/VERSION_NOTES_v0.2.0.md",
        "docs/NEXT_DEVELOP_PLAN_v0.3.0.md", "docs/CONTINUATION_GUIDE.md",
        "generated/Chinese_IME_Test_Corpus_v0.2.0.md", "generated/Chinese_IME_Test_Corpus_v0.2.0.txt"
    ]
    for rel in required:
        if not (ROOT / rel).exists(): errors.append(f"missing required file: {rel}")
    if errors:
        print("VALIDATION FAILED")
        for e in errors: print(f"- {e}")
        return 1
    print("VALIDATION PASSED")
    print(f"- standard pinyin syllables: {len(inventory)}")
    print(f"- structured test cases: {test_count}")
    print(f"- language-model cases: {len(load(ROOT / 'tests/language_model_test_cases.json'))}")
    print(f"- professional vocabulary cases: {len(load(ROOT / 'tests/professional_vocabulary_test_cases.json'))}")
    print(f"- correction cases: {len(corr)}")
    print(f"- fuzzy-pinyin cases: {len(fuzzy)}")
    print(f"- user-learning cases: {len(load(ROOT / 'tests/user_learning_test_cases.json'))}")
    return 0
if __name__ == "__main__":
    sys.exit(main())
