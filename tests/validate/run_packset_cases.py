#!/usr/bin/env python3
"""Pack-set resolution cases for tools/validate (ctest: content_validate_packset).

Builds small packs in a temporary dir from the sample pack's documents and runs the validator
over them as one pack set. Also unit-checks packset.py (range dialect, deterministic order).
"""
from __future__ import annotations

import json
import random
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
VALIDATOR = ROOT / "tools" / "validate" / "validate_packs.py"
SAMPLE = ROOT / "content" / "packs" / "sample.vertical_slice"
sys.path.insert(0, str(ROOT / "tools" / "validate"))
import packset  # noqa: E402

BASE_MAN = json.loads((SAMPLE / "manifest.json").read_text(encoding="utf-8"))


def doc(rel: str) -> dict:
    d = json.loads((SAMPLE / rel).read_text(encoding="utf-8"))
    d.pop("behavior", None)   # no scripts in these packs
    return d


GOBLIN = ("monsters/goblin.json", doc("monsters/goblin.json"))
FIGHTER = ("characters/fighter_1.json", doc("characters/fighter_1.json"))
CHUNK = ("chunks/0_0_0.json", doc("chunks/0_0_0.json"))   # spawns fighter_1 + goblin


def make(root: Path, dirname: str, pid: str, version: str = "1.2.0", depends=(), files=()) -> Path:
    pack = root / dirname
    for rel, d in files:
        (pack / rel).parent.mkdir(parents=True, exist_ok=True)
        (pack / rel).write_text(json.dumps(d, indent=2), encoding="utf-8")
    man = dict(BASE_MAN, id=pid, version=version, contents=sorted(rel for rel, _ in files),
               depends=[{"id": i, "version": r} for i, r in depends])
    man.pop("entry_script", None)
    pack.mkdir(parents=True, exist_ok=True)
    (pack / "manifest.json").write_text(json.dumps(man, indent=2), encoding="utf-8")
    return pack


def run(*packs: Path) -> tuple[int, str]:
    r = subprocess.run([sys.executable, str(VALIDATOR), *map(str, packs)], capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def cases(t: Path):
    """(name, packs, expect_ok, substring)."""
    yield ("transitive_deps_resolve",
           [make(t / "a1", "a", "test.a", depends=[("test.b", "^1.0")], files=[CHUNK, FIGHTER]),
            make(t / "a1", "b", "test.b", depends=[("test.c", "~1.2.0")]),
            make(t / "a1", "c", "test.c", files=[GOBLIN])], True, "")
    yield ("missing_dep",
           [make(t / "a2", "a", "test.a", depends=[("test.nowhere", "^1.0")], files=[FIGHTER])],
           False, "missing_dep: test.a: depends on 'test.nowhere'")
    yield ("version_mismatch",
           [make(t / "a3", "a", "test.a", depends=[("test.c", "^2.0")], files=[FIGHTER]),
            make(t / "a3", "c", "test.c", files=[GOBLIN])], False, "version_mismatch")
    yield ("prerelease_never_satisfies",
           [make(t / "a4", "a", "test.a", depends=[("test.c", "^1.0")], files=[FIGHTER]),
            make(t / "a4", "c", "test.c", version="1.2.0-rc1", files=[GOBLIN])], False, "version_mismatch")
    yield ("range_outside_dialect",
           [make(t / "a5", "a", "test.a", depends=[("test.c", "^1")], files=[FIGHTER]),
            make(t / "a5", "c", "test.c", files=[GOBLIN])], False, "bad_range")
    yield ("cycle",
           [make(t / "a6", "a", "test.a", depends=[("test.b", "^1.0")], files=[FIGHTER]),
            make(t / "a6", "b", "test.b", depends=[("test.a", "^1.0")])], False, "dependency cycle")
    yield ("duplicate_pack_id",
           [make(t / "a7", "x1", "test.dup", files=[FIGHTER]), make(t / "a7", "x2", "test.dup")],
           False, "duplicate_pack_id")
    yield ("duplicate_content_id_across_set",
           [make(t / "a8", "a", "test.a", depends=[("test.c", "^1.0")], files=[GOBLIN, FIGHTER]),
            make(t / "a8", "c", "test.c", files=[GOBLIN])], False, "duplicate_content_id")
    yield ("ref_only_in_non_dependency",
           [make(t / "a9", "a", "test.a", depends=[("srd51.core", "^1.0")], files=[CHUNK, FIGHTER]),
            make(t / "a9", "core", "srd51.core", files=[]),
            make(t / "a9", "other", "test.other", files=[GOBLIN])],
           False, "archetype 'srd51.monster.goblin' not defined in pack or its depends")
    yield ("srd51_escape_hatch_warns_without_core",
           [make(t / "a10", "a", "test.a", files=[CHUNK, FIGHTER])], True, "escape hatch is deprecated")


def unit_checks() -> list[str]:
    errs = []
    for v, r, want in [("1.2.3", "1.2.3", True), ("1.2.4", "1.2.3", False), ("1.9.0", "^1.0", True),
                       ("2.0.0", "^1.0", False), ("0.2.9", "^0.2.3", True), ("0.3.0", "^0.2.3", False),
                       ("0.0.4", "^0.0.3", False), ("1.2.9", "~1.2.3", True), ("1.3.0", "~1.2.3", False),
                       ("7.0.0", ">=1.2.3", True), ("1.2.2", ">=1.2.3", False), ("1.0.0-rc.1", "^1.0", False)]:
        if packset.satisfies(v, r) != want:
            errs.append(f"satisfies({v!r}, {r!r}) != {want}")
    for bad in ["1", "1.2", "~1.2", ">=1.2", "^1", ">1.2.3", "1.2.3-rc1", "^01.0"]:
        if packset.parse_range(bad) is not None:
            errs.append(f"range {bad!r} should be outside the dialect")
    with tempfile.TemporaryDirectory(prefix="packset_order_") as tmp:
        t = Path(tmp)
        dirs = [make(t, "z", "test.z", depends=[("test.m", "^1.0"), ("test.b", "^1.0")]),
                make(t, "m", "test.m", depends=[("test.a", "^1.0")]),
                make(t, "b", "test.b"), make(t, "a", "test.a")]
        want = ["test.a", "test.b", "test.m", "test.z"]   # deps first, ties by pack id
        for seed in range(8):
            random.Random(seed).shuffle(dirs)
            order, e = packset.PackSet(list(dirs)).resolve(["test.z"])
            if [p.id for p in order] != want or e:
                errs.append(f"order {[p.id for p in order]} != {want} (shuffle {seed}) {e}")
    return errs


def main() -> int:
    failures = 0
    for e in unit_checks():
        print(f"FAIL unit: {e}")
        failures += 1
    n = 0
    with tempfile.TemporaryDirectory(prefix="packset_cases_") as tmp:
        for name, packs, expect_ok, sub in cases(Path(tmp)):
            n += 1
            rc, out = run(*packs)
            ok = (rc == 0) == expect_ok and sub in out
            failures += not ok
            print(("ok   " if ok else "FAIL ") + f"packset case {name}" + ("" if ok else f"\n{out}"))
    print(f"{n - (failures if failures <= n else n)}/{n} packset cases (+ unit checks), {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
