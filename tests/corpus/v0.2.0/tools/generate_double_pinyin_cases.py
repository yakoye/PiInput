#!/usr/bin/env python3
"""Generate Xiaohe double-pinyin codes from explicit pinyin syllable arrays.
从显式拼音音节数组生成小鹤双拼编码，避免手工维护长句错码。
"""

from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def load_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


def split_initial_final(syllable: str):
    s = syllable.lower().replace("ü", "v")
    for ini in ("zh", "ch", "sh"):
        if s.startswith(ini):
            return ini, s[len(ini):]
    if s and s[0] in "bpmfdtnlgkhjqxrzcsyw":
        return s[0], s[1:]
    return "", s


def encode_syllable(syllable: str, scheme: dict) -> str:
    s = syllable.lower().replace("ü", "v")
    zero = scheme["zero_initial_map"]
    if s in zero:
        return zero[s]
    ini, fin = split_initial_final(s)
    if not ini:
        raise ValueError(f"unsupported zero-initial syllable: {syllable}")
    try:
        return scheme["initial_map"][ini] + scheme["final_map"][fin]
    except KeyError as exc:
        raise ValueError(f"cannot encode {syllable}: initial={ini}, final={fin}") from exc


def main() -> int:
    scheme = load_json(ROOT / "schemes" / "xiaohe.json")
    sentences = load_json(ROOT / "corpus" / "long_sentences.json")
    output = []
    for item in sentences:
        codes = [encode_syllable(s, scheme) for s in item["pinyin_syllables"]]
        output.append({
            "id": item["id"],
            "target": item["target"],
            "pinyin_syllables": item["pinyin_syllables"],
            "xiaohe_syllable_codes": codes,
            "xiaohe_continuous": "".join(codes),
        })
    out_path = ROOT / "generated" / "xiaohe_long_sentences.generated.json"
    out_path.write_text(json.dumps(output, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"generated: {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
