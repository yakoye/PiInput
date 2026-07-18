#!/usr/bin/env python3
"""Materialize fuzzy-pinyin switch tests from corpus data.
从语料生成模糊音开启与关闭的成对测试。
"""
from __future__ import annotations
import json
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]

def main() -> int:
    data = json.loads((ROOT / "corpus/fuzzy_pinyin.json").read_text(encoding="utf-8"))
    out = ROOT / "tests/fuzzy_pinyin_test_cases.json"
    out.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"generated fuzzy-pinyin cases: {len(data)}")
    return 0
if __name__ == "__main__":
    raise SystemExit(main())
