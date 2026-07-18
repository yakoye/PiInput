#!/usr/bin/env python3
"""Evaluate engine candidate results with Top-K, rank, exact-match and CER metrics.
使用 Top-K、目标排名、整句匹配和字符错误率评估输入法结果。
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def load(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))

def levenshtein(a: str, b: str) -> int:
    prev = list(range(len(b) + 1))
    for i, ca in enumerate(a, 1):
        cur = [i]
        for j, cb in enumerate(b, 1):
            cur.append(min(cur[-1] + 1, prev[j] + 1, prev[j-1] + (ca != cb)))
        prev = cur
    return prev[-1]

def percentile(values, p):
    if not values: return None
    vals = sorted(values)
    idx = min(len(vals)-1, max(0, round((len(vals)-1) * p)))
    return vals[idx]

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("results", nargs="?", default=str(ROOT / "reports/sample_engine_results.json"))
    ap.add_argument("--out", default=str(ROOT / "generated/sample_evaluation_report.json"))
    args = ap.parse_args()
    expected = {}
    for rel in ["tests/language_model_test_cases.json", "tests/professional_vocabulary_test_cases.json"]:
        for item in load(ROOT / rel): expected[item["id"]] = item
    rows = []
    for result in load(Path(args.results)):
        case = expected.get(result["id"])
        if not case: continue
        target = case["target_text"]
        candidates = result.get("candidates", [])
        rank = candidates.index(target) + 1 if target in candidates else None
        committed = result.get("committed_text", candidates[0] if candidates else "")
        cer = levenshtein(target, committed) / max(1, len(target))
        rows.append({
            "id": result["id"], "target_text": target, "target_rank": rank,
            "top1": rank == 1, "top5": rank is not None and rank <= 5,
            "exact_match": committed == target, "character_error_rate": round(cer, 6),
            "candidate_latency_ms": result.get("candidate_latency_ms")
        })
    latencies = [r["candidate_latency_ms"] for r in rows if isinstance(r["candidate_latency_ms"], (int, float))]
    summary = {
        "sample_only": True,
        "case_count": len(rows),
        "top1_accuracy": sum(r["top1"] for r in rows) / len(rows) if rows else None,
        "top5_accuracy": sum(r["top5"] for r in rows) / len(rows) if rows else None,
        "exact_match_rate": sum(r["exact_match"] for r in rows) / len(rows) if rows else None,
        "mean_character_error_rate": sum(r["character_error_rate"] for r in rows) / len(rows) if rows else None,
        "candidate_latency_ms_p50": percentile(latencies, 0.50),
        "candidate_latency_ms_p95": percentile(latencies, 0.95),
    }
    output = {"summary": summary, "cases": rows}
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(output, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0
if __name__ == "__main__":
    raise SystemExit(main())
