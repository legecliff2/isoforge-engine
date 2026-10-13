#!/usr/bin/env python3
"""Negative/warning cases for feature effects (character@1/@2/@3, monsters).

Each effect_cases/<name>.json describes an edit of a sample-pack document:
  {"base": "<pack-relative file>", "schema": "<optional $schema override>",
   "sets": [[<path>, <value>], ...], "dels": [<path>, ...],
   "severity": "error" | "warning", "expect": "<substring the validator must print>"}
The edited document replaces the original in a temporary copy of the sample pack (unique dir per
case). "error": the validator must fail and print `expect`. "warning": it must pass and print
`expect` on a warning line.

Run by content_validate (validate_packs.py with no pack arguments) until #23's
tests/validate/run_negative.py framework lands; then fold these into it as their own ctest.
"""
from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
VALIDATOR = ROOT / "tools" / "validate" / "validate_packs.py"
SAMPLE = ROOT / "content" / "packs" / "sample.vertical_slice"


def walk(doc, path):
    for k in path[:-1]:
        doc = doc[k]
    return doc, path[-1]


def apply(doc: dict, case: dict) -> dict:
    if "schema" in case:
        doc["$schema"] = case["schema"]
    for path, value in case.get("sets", []):
        parent, k = walk(doc, path)
        if isinstance(parent, list) and k == len(parent):
            parent.append(value)
        else:
            parent[k] = value
    for path in case.get("dels", []):
        parent, k = walk(doc, path)
        del parent[k]
    return doc


def main() -> int:
    cases = sorted((HERE / "effect_cases").glob("*.json"))
    failures = 0
    with tempfile.TemporaryDirectory(prefix="effect_cases_") as tmp:
        for case_path in cases:
            case = json.loads(case_path.read_text(encoding="utf-8"))
            pack = Path(tmp) / case_path.stem / SAMPLE.name
            shutil.copytree(SAMPLE, pack)
            target = pack / case["base"]
            target.write_text(json.dumps(apply(json.loads(target.read_text(encoding="utf-8")), case), indent=2),
                              encoding="utf-8")
            r = subprocess.run([sys.executable, str(VALIDATOR), str(pack)], capture_output=True, text=True)
            out = r.stdout + r.stderr
            if case["severity"] == "error":
                ok = r.returncode != 0 and case["expect"] in out
            else:
                ok = r.returncode == 0 and any(l.startswith("warning") and case["expect"] in l for l in out.splitlines())
            failures += not ok
            print(("ok   " if ok else "FAIL ") + f"effect case {case_path.stem}" + ("" if ok else f"\n{out}"))
    print(f"{len(cases) - failures}/{len(cases)} effect cases passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
