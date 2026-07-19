#!/usr/bin/env python3
"""Regenerate all derived files, validate, create manifest and release ZIP.
重新生成派生文件、执行校验、生成清单并创建发布压缩包。
"""
from __future__ import annotations
import subprocess, sys, zipfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = [
    "generate_double_pinyin_cases.py", "generate_language_model_cases.py", "generate_typo_cases.py",
    "generate_fuzzy_cases.py", "generate_human_corpus.py", "evaluate_candidate_results.py",
    "validate_test_corpus.py", "generate_manifest.py"
]

def main() -> int:
    for name in SCRIPTS:
        subprocess.run([sys.executable, str(ROOT / "tools" / name)], check=True, cwd=ROOT)
    zip_path = ROOT.parent / "piinput-test-corpus-v0.2.0.zip"
    if zip_path.exists(): zip_path.unlink()
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for p in sorted(ROOT.rglob("*")):
            if p.is_file() and "__pycache__" not in p.parts:
                zf.write(p, Path(ROOT.name) / p.relative_to(ROOT))
    print(f"created: {zip_path}")
    return 0
if __name__ == "__main__": raise SystemExit(main())
