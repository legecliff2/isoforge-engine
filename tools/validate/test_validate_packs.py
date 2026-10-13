"""Unit tests for validate_packs.py (run by the content_validate_tests ctest)."""
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import validate_packs as vp  # noqa: E402

SAMPLE = vp.ROOT / "content" / "packs" / "sample.vertical_slice"
CORE = vp.ROOT / "content" / "packs" / "srd51.core"   # the sample depends on it
FIGHTER = "characters/fighter_1.json"


class EffectKinds(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.registry, cls.by_tag = vp.load_registry()

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="validate_packs_test_"))   # unique per test
        self.pack = self.tmp / SAMPLE.name
        shutil.copytree(SAMPLE, self.pack)
        self.core = self.tmp / CORE.name
        shutil.copytree(CORE, self.core)

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def edit_fighter(self, fn):
        p = self.pack / FIGHTER
        doc = json.loads(p.read_text(encoding="utf-8"))
        fn(doc)
        p.write_text(json.dumps(doc, indent=2), encoding="utf-8")

    def run_pack(self):
        warns = []
        errs = vp.validate_pack(self.pack, self.registry, self.by_tag, warns, vp.packset.PackSet([self.core, self.pack]))
        return errs, warns

    def test_sample_pack_clean(self):
        errs, warns = self.run_pack()
        self.assertEqual(errs, [])
        self.assertEqual(warns, [])

    def test_unknown_kind_is_warning_not_error(self):
        self.edit_fighter(lambda d: d["features"].append(
            {"id": "mystery", "name": "Mystery", "effect": {"kind": "not_a_hook", "foo": 1}}))
        errs, warns = self.run_pack()
        self.assertEqual(errs, [])
        self.assertEqual(len(warns), 1)
        self.assertIn("unknown effect kind 'not_a_hook'", warns[0])
        self.assertIn(FIGHTER, warns[0])

    def test_known_kind_fields_are_checked(self):
        cases = [
            ("damage_bonus amount must be integer", 0, "amount", "2"),
            ("damage_bonus condition must be known", 0, "condition", "no_such_condition"),
            ("self_heal cost enum", 1, "cost", "free"),
            ("self_heal recharge enum", 1, "recharge", "dawn"),
            ("self_heal plus form", 1, "plus", "half_level!"),
        ]
        for name, idx, key, val in cases:
            with self.subTest(name):
                self.setUp()
                self.edit_fighter(lambda d: d["features"][idx]["effect"].__setitem__(key, val))
                errs, _ = self.run_pack()
                self.assertTrue(any(FIGHTER in e for e in errs), errs)
                self.tearDown()
        self.setUp()

    def test_cost_values_accepted(self):
        for cost in ("action", "bonus_action"):
            with self.subTest(cost):
                self.edit_fighter(lambda d: d["features"][1]["effect"].__setitem__("cost", cost))
                self.assertEqual(self.run_pack()[0], [])
        # character@3: 'reaction' is reserved (clear error); see tests/validate/effect_cases.
        self.edit_fighter(lambda d: d["features"][1]["effect"].__setitem__("cost", "reaction"))
        self.assertTrue(any("reserved" in e for e in self.run_pack()[0]))

    def test_missing_required_field(self):
        self.edit_fighter(lambda d: d["features"][1]["effect"].pop("dice"))
        errs, _ = self.run_pack()
        self.assertTrue(any("dice" in e for e in errs), errs)


if __name__ == "__main__":
    unittest.main()
