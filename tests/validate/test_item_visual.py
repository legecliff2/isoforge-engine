#!/usr/bin/env python3
"""item@1 optional `visual` block (ctest: content_validate_item_visual; docs/proposals/LIGHTING.md 10.1).

visual: {color: {r, g, b} (integers 0..255), intensity (integer permille 0..1000), flicker?: none|torch|candle},
additionalProperties false at each level, integers only. Presentation only: additive (item@1 unchanged),
excluded from content_hash, never read by sim (so never in log_hash).
"""
from __future__ import annotations

import copy
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCHEMAS = ROOT / "schemas"
sys.path.insert(0, str(ROOT / "tools" / "validate"))
import validate_packs as vp  # noqa: E402


def load(rel):
    return json.loads((SCHEMAS / rel).read_text(encoding="utf-8"))


def validator():
    from jsonschema import Draft202012Validator
    from referencing import Registry, Resource
    res = []
    for p in SCHEMAS.rglob("*.schema.json"):
        d = json.loads(p.read_text(encoding="utf-8"))
        if "$id" in d:
            res.append((d["$id"], Resource.from_contents(d)))
    return Draft202012Validator(load("item/v1.schema.json"), registry=Registry().with_resources(res))


BASE = {"$schema": "item@1", "id": "test.item.lantern", "name": "Lantern", "category": "valuable", "cost_cp": 500}
GOOD = {"color": {"r": 255, "g": 170, "b": 70}, "intensity": 800, "flicker": "torch"}


class ItemVisualSchema(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.v = validator()

    def ok(self, doc):
        errs = list(self.v.iter_errors(doc))
        self.assertEqual(errs, [], [e.message for e in errs])

    def bad(self, doc):
        self.assertTrue(list(self.v.iter_errors(doc)), f"expected rejection: {doc.get('visual')}")

    def test_accepts(self):
        self.ok(BASE)                                            # visual is optional
        self.ok({**BASE, "visual": GOOD})
        for f in ("none", "torch", "candle"):
            self.ok({**BASE, "visual": {**GOOD, "flicker": f}})
        no_flicker = {k: v for k, v in GOOD.items() if k != "flicker"}
        self.ok({**BASE, "visual": no_flicker})
        self.ok({**BASE, "visual": {"color": {"r": 0, "g": 0, "b": 0}, "intensity": 0}})
        self.ok({**BASE, "visual": {"color": {"r": 255, "g": 255, "b": 255}, "intensity": 1000}})
        # every category arm allows it (closed propertyNames lists)
        club = json.loads((ROOT / "content/packs/srd51.core/items/club.json").read_text(encoding="utf-8"))
        self.ok({**club, "visual": GOOD})

    def test_rejects(self):
        def with_(**kw):
            v = copy.deepcopy(GOOD)
            v.update(kw)
            return {**BASE, "visual": v}
        self.bad(with_(intensity=1001))
        self.bad(with_(intensity=-1))
        self.bad(with_(intensity=0.5))                          # integers only
        self.bad(with_(flicker="strobe"))
        self.bad(with_(extra=1))                                # additionalProperties: false (visual)
        self.bad(with_(color={"r": 256, "g": 0, "b": 0}))
        self.bad(with_(color={"r": -1, "g": 0, "b": 0}))
        self.bad(with_(color={"r": 1.5, "g": 0, "b": 0}))        # no floats (JSON Schema "integer" admits 1.0; see validator check)
        self.bad(with_(color="#ffaa46"))                        # no hex
        self.bad(with_(color={"r": 1, "g": 2}))                 # all channels required
        self.bad(with_(color={"r": 1, "g": 2, "b": 3, "a": 4})) # additionalProperties: false (color)
        self.bad({**BASE, "visual": {"color": GOOD["color"]}})  # intensity required
        self.bad({**BASE, "visual": {"intensity": 500}})        # color required

    def test_validator_rejects_integral_floats(self):
        # JSON Schema "integer" admits 1.0; the validator's section 4.3 float lint catches it.
        errs: list[str] = []
        vp.reject_floats({**BASE, "visual": {**GOOD, "color": {"r": 1.0, "g": 0, "b": 0}}}, "", errs)
        self.assertTrue(any("/visual/color/r" in e for e in errs), errs)

    def test_not_a_version_bump(self):
        self.assertEqual(load("item/v1.schema.json")["properties"]["$schema"], {"const": "item@1"})


class ItemVisualHash(unittest.TestCase):
    def test_validator_accepts_and_content_hash_unchanged(self):
        sample = ROOT / "content/packs/sample.vertical_slice"
        with tempfile.TemporaryDirectory() as tmp:
            hashes = []
            for with_visual in (False, True):
                root = Path(tmp) / ("v" if with_visual else "n")
                pack = root / "test.visual"
                shutil.copytree(sample, pack)
                man = json.loads((pack / "manifest.json").read_text(encoding="utf-8"))
                man["id"] = "test.visual"
                man["api_level"] = 2   # item@1 documents need api_level >= 2
                man["contents"] = sorted(man["contents"] + ["items/lantern.json"])
                (pack / "manifest.json").write_text(json.dumps(man, indent=2), encoding="utf-8")
                (pack / "items").mkdir()
                item = {**BASE, "id": "test.visual.item.lantern"}
                if with_visual:
                    item["visual"] = GOOD
                (pack / "items/lantern.json").write_text(json.dumps(item, indent=2), encoding="utf-8")
                r = subprocess.run([sys.executable, str(ROOT / "tools/validate/validate_packs.py"),
                                    str(pack), str(ROOT / "content/packs/srd51.core")],
                                   capture_output=True, text=True, cwd=ROOT)
                self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
                chunk = json.loads((pack / "chunks/0_0_0.json").read_text(encoding="utf-8"))
                hashes.append((vp.compute_content_hash(chunk), chunk.get("content_hash")))
            self.assertEqual(hashes[0], hashes[1])
            self.assertEqual(hashes[1][1], vp.hash_hex(hashes[1][0]))   # committed hash still matches


if __name__ == "__main__":
    unittest.main()
