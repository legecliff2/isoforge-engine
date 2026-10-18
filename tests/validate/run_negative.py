#!/usr/bin/env python3
"""Negative tests for tools/validate/validate_packs.py (run by ctest: content_validate_negative).

Each cases/<name>.json is one bad spell@1 document; cases/<name>.expect holds a substring the
validator must print. The doc is placed alone in a temporary pack (namespace 'test.neg'), next to
the sample pack's spells so references resolve like in real content. Also checks runtime-id
collisions within a pack and across a pack set.
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
ATTRIBUTION = json.loads((ROOT / "content/packs/sample.vertical_slice/manifest.json").read_text(encoding="utf-8"))["attribution"]
SAMPLE_SPELLS = ROOT / "content" / "packs" / "srd51.core" / "spells"


def make_pack(root: Path, pack_id: str, docs: dict[str, dict], api_level: int = 1) -> Path:
    pack = root / pack_id
    (pack / "spells").mkdir(parents=True)
    for name, doc in docs.items():
        (pack / "spells" / f"{name}.json").write_text(json.dumps(doc, indent=2), encoding="utf-8")
    manifest = {"$schema": "manifest@1", "id": pack_id, "name": "negative test", "version": "1.0.0",
                "engine_min": "0.1.0", "feature_level": 1, "api_level": api_level, "rules_profile": "srd51",
                "requires": [], "optional": [], "depends": [], "attribution": ATTRIBUTION,
                "contents": sorted(f"spells/{n}.json" for n in docs)}
    (pack / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    return pack


def run(*packs: Path) -> tuple[int, str]:
    r = subprocess.run([sys.executable, str(VALIDATOR), *map(str, packs)], capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def spell(name: str, cid: str) -> dict:
    d = json.loads((SAMPLE_SPELLS / f"{name}.json").read_text(encoding="utf-8"))
    d["id"] = cid
    return d


def main() -> int:
    failures = 0
    cases = sorted((HERE / "cases").glob("*.json"))
    tmp = Path(tempfile.mkdtemp(prefix="validate_neg_"))
    extra = ()
    try:
        # Sanity: the sample spells alone are valid in a test pack.
        rc, out = run(make_pack(tmp / "ok", "test.neg", {p.stem: spell(p.stem, f"test.neg.spell.{p.stem}")
                                                         for p in sorted(SAMPLE_SPELLS.glob("*.json"))}))
        if rc != 0:
            print(f"FAIL sanity pack should be valid:\n{out}")
            failures += 1
        for case in cases:
            doc = json.loads(case.read_text(encoding="utf-8"))
            expect = case.with_suffix(".expect").read_text(encoding="utf-8").strip()
            rc, out = run(make_pack(tmp / case.stem, "test.neg", {case.stem: doc}))
            if rc == 0 or expect not in out:
                print(f"FAIL {case.stem}: expected failure containing {expect!r}\n{out}")
                failures += 1
            else:
                print(f"ok   {case.stem}")
        # Runtime id collisions (FNV-1a 32): 'srd51.spell.x562789' and 'srd51.spell.x779192' collide.
        a, b = spell("fire_bolt", "test.neg.spell.x562789"), spell("fire_bolt", "test.neg.spell.x779192")
        a["id"], b["id"] = "srd51.spell.x562789", "srd51.spell.x779192"
        rc, out = run(make_pack(tmp / "coll_in", "test.neg", {"a": a, "b": b}))
        ok_in = rc != 0 and "collides with" in out
        rc, out2 = run(make_pack(tmp / "coll_x1", "test.nega", {"a": a}), make_pack(tmp / "coll_x2", "test.negb", {"b": b}))
        ok_x = rc != 0 and "FAIL pack set" in out2
        # Same folder name in different places (like tests/compat/fl1/<pack> vs content/packs/<pack>):
        # packs are keyed by resolved path, so this is still a cross-pack collision.
        rc, out3 = run(make_pack(tmp / "dir1", "test.neg", {"a": a}), make_pack(tmp / "dir2", "test.neg", {"b": b}))
        ok_same = rc != 0 and "FAIL pack set" in out3 and "collides with" in out3
        # The same content id in two packs is an error too (rules5e_content::add_spell agrees).
        rc, out4 = run(make_pack(tmp / "dup1", "test.nega", {"a": a}), make_pack(tmp / "dup2", "test.negb", {"a": a}))
        ok_dup = rc != 0 and "duplicate content id" in out4
        # Pack-set errors make the summary say FAILED, not "N/N packs valid".
        ok_sum = "FAILED: 2/2 packs valid" in out4 and "\n2/2 packs valid" not in out4
        # A frozen compat copy (tests/compat/<level>/) is its own pack set: no duplicate vs the live pack.
        rc5, out5 = run(make_pack(tmp / "live" / "dupl", "test.nega", {"a": a}),
                        make_pack(tmp / "tests" / "compat" / "fl9" / "dupl", "test.nega", {"a": a}))
        ok_compat = rc5 == 0
        # Manifest api_level: levels 1 and 2 are valid (a pack runs at its own level), level 3 (and 0) is not.
        one = {"fire_bolt": spell("fire_bolt", "test.neg.spell.fire_bolt")}
        rc_l1, out_l1 = run(make_pack(tmp / "lvl1", "test.neg", one, api_level=1))
        rc_l2, out_l2 = run(make_pack(tmp / "lvl2", "test.neg", one, api_level=2))
        rc_l3, out_l3 = run(make_pack(tmp / "lvl3", "test.neg", one, api_level=3))
        rc_l0, out_l0 = run(make_pack(tmp / "lvl0", "test.neg", one, api_level=0))
        extra_levels = (("api_level_1_ok", rc_l1 == 0, out_l1), ("api_level_2_ok", rc_l2 == 0, out_l2),
                        ("api_level_3_rejected", rc_l3 != 0 and "api_level" in out_l3, out_l3),
                        ("api_level_0_rejected", rc_l0 != 0 and "api_level" in out_l0, out_l0))
        extra = (("collision_within_pack", ok_in, out), ("collision_across_packs", ok_x, out2),
                 ("collision_same_folder_name", ok_same, out3), ("duplicate_id_across_packs", ok_dup, out4),
                 ("pack_set_error_summary", ok_sum, out4), ("compat_copy_own_pack_set", ok_compat, out5)) + extra_levels
        for name, ok, o in extra:
            print(("ok   " if ok else "FAIL ") + name + ("" if ok else "\n" + o))
            failures += not ok
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print(f"{len(cases) + len(extra) - failures}/{len(cases) + len(extra)} negative checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
