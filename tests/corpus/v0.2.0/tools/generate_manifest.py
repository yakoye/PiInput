#!/usr/bin/env python3
"""Generate release manifest with SHA-256 hashes."""
from __future__ import annotations
import hashlib, json
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]

def main() -> int:
    files = []
    for p in sorted(ROOT.rglob("*")):
        if not p.is_file() or p.name == "MANIFEST.json" or "__pycache__" in p.parts: continue
        data = p.read_bytes()
        files.append({"path": p.relative_to(ROOT).as_posix(), "size": len(data), "sha256": hashlib.sha256(data).hexdigest()})
    manifest = {"package":"lite-ime-test-corpus-v0.2.0","version":"0.2.0","encoding":"UTF-8","file_count":len(files),"files":files}
    (ROOT / "MANIFEST.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    print(f"generated manifest: {len(files)} files")
    return 0
if __name__ == "__main__": raise SystemExit(main())
