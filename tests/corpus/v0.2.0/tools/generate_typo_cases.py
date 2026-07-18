#!/usr/bin/env python3
"""Generate deterministic correction cases for full pinyin and Xiaohe.
为全拼和小鹤双拼生成可追溯的相邻键、漏键、重复键和交换键用例。
"""
from __future__ import annotations
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def load(rel):
    return json.loads((ROOT / rel).read_text(encoding="utf-8"))

def split_initial_final(syllable: str):
    s = syllable.lower().replace("ü", "v")
    for ini in ("zh", "ch", "sh"):
        if s.startswith(ini): return ini, s[len(ini):]
    if s and s[0] in "bpmfdtnlgkhjqxrzcsyw": return s[0], s[1:]
    return "", s

def encode_xiaohe(syllable: str, scheme: dict) -> str:
    s = syllable.lower().replace("ü", "v")
    if s in scheme["zero_initial_map"]: return scheme["zero_initial_map"][s]
    ini, fin = split_initial_final(s)
    return scheme["initial_map"][ini] + scheme["final_map"][fin]

def mutate(source: str, adjacency: dict):
    if len(source) < 2: return []
    mid = len(source) // 2
    replace_index = next((i for i, ch in enumerate(source) if adjacency.get(ch.lower())), 0)
    ch = source[replace_index]
    replacement = adjacency.get(ch.lower(), [ch])[0]
    swap_index = max(0, mid - 1)
    return [
        ("adjacent_replacement", source[:replace_index] + replacement + source[replace_index+1:], replace_index,
         {"original": ch, "replacement": replacement}),
        ("missing_key", source[:mid] + source[mid+1:], mid, {"removed": source[mid]}),
        ("repeated_key", source[:mid] + source[mid] + source[mid:], mid, {"repeated": source[mid]}),
        ("transposed_keys", source[:swap_index] + source[swap_index+1] + source[swap_index] + source[swap_index+2:],
         swap_index, {"pair": source[swap_index:swap_index+2]}),
    ]

def main() -> int:
    adjacency = load("keyboard/qwerty_adjacency.json")["adjacency"]
    scheme = load("schemes/xiaohe.json")
    seeds = load("corpus/correction_seeds.json")
    result = []
    n = 1
    for seed in seeds:
        if seed["scheme"] == "xiaohe":
            source = "".join(encode_xiaohe(s, scheme) for s in seed["pinyin_syllables"])
        else:
            source = seed["source_input"]
        for kind, mutated, index, detail in mutate(source, adjacency):
            result.append({
                "id": f"CORR-{n:04d}", "seed_id": seed["id"], "scheme": seed["scheme"],
                "target_text": seed["target"], "source_input": source, "mutated_input": mutated,
                "mutation_type": kind, "mutation_index": index, "mutation_detail": detail,
                "edit_distance_expected": 1,
                "expected": {
                    "correction_disabled": ["stable_raw_behavior", "no_crash", "composition_can_be_cleared"],
                    "correction_enabled": ["record_target_rank", "target_reachability_is_product_policy", "no_silent_data_loss"]
                },
                "priority": "P1"
            })
            n += 1
    out = ROOT / "tests/correction_test_cases.json"
    out.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"generated correction cases: {len(result)}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
