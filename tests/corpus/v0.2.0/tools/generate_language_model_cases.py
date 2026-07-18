#!/usr/bin/env python3
"""Generate structured language-model and professional-vocabulary test cases.
从语料生成长句、上下文消歧、切分歧义和专业词汇测试用例。
"""
from __future__ import annotations
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def load(rel):
    return json.loads((ROOT / rel).read_text(encoding="utf-8"))

def dump(rel, data):
    (ROOT / rel).write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

def main() -> int:
    cases = []
    for item in load("corpus/long_sentences.json"):
        cases.append({
            "id": f"LM-{item['id']}", "category": "long_sentence", "scheme": "full_pinyin",
            "input_sequence": item["full_pinyin_continuous"], "target_text": item["target"],
            "pinyin_syllables": item["pinyin_syllables"], "test_points": item.get("test_points", []),
            "metrics": {"record_top1": True, "record_top5": True, "record_target_rank": True,
                        "record_exact_match": True, "record_character_error_rate": True,
                        "record_candidate_latency_ms": True}, "priority": "P1"
        })
    for item in load("corpus/context_disambiguation.json"):
        cases.append({
            "id": f"LM-{item['id']}", "category": "context_disambiguation", "scheme": "full_pinyin",
            "pair_id": item["pair_id"], "input_sequence": item["full_pinyin_continuous"],
            "ambiguous_input": item["ambiguous_input"], "target_text": item["target_sentence"],
            "target_word": item["target_word"], "contrast_word": item["contrast_word"],
            "metrics": {"record_top1": True, "record_top5": True, "record_target_rank": True,
                        "record_exact_match": True, "record_character_error_rate": True}, "priority": "P1"
        })
    for item in load("corpus/segmentation_ambiguity.json"):
        cases.append({
            "id": f"LM-{item['id']}", "category": "segmentation_ambiguity", "scheme": "full_pinyin",
            "input_sequence": item["input"], "target_text": item["target"], "context": item["context"],
            "forced_input": item.get("forced_input"), "possible_paths": item.get("possible_paths", []),
            "metrics": {"record_target_rank": True, "record_top5": True}, "priority": "P1"
        })
    prof = []
    for item in load("corpus/professional_terms.json"):
        prof.append({
            "id": f"VOCAB-{item['id']}", "category": "professional_vocabulary", "domain": item["domain"],
            "scheme": "full_pinyin", "input_sequence": item["full_pinyin_continuous"],
            "target_text": item["text"], "pinyin_syllables": item["pinyin_syllables"],
            "expected": item["expected_metrics"], "priority": "P1"
        })
    dump("tests/language_model_test_cases.json", cases)
    dump("tests/professional_vocabulary_test_cases.json", prof)
    print(f"generated language-model cases: {len(cases)}")
    print(f"generated professional vocabulary cases: {len(prof)}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
