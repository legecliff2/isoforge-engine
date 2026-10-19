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


KINDS_FIXTURES = vp.ROOT / "tests" / "validate" / "fixtures" / "spell_kinds" / "spells"
SLEEP, SPRAY, DISPEL, REVIVE, LR, ACID, HEAT = ("sleep", "color_spray", "dispel_magic", "revivify", "lesser_restoration",
                                                "acid_arrow", "heat_metal")
E0, E1 = ["effects", 0], ["effects", 1]
# R-G15 spell@1 kinds wired by sim #120 (docs/CONTENT_CONVENTIONS.md section 5). (name, fixture, sets, dels, expected substring).
# Cross-field messages are the loader's (spell_loader.cpp) verbatim.
KIND_CASES = [
    # hp_pool
    ("hp_pool_needs_dice", SLEEP, [], [E0 + ["dice"]], "'dice' is a required property"),
    ("hp_pool_flat_dice", SLEEP, [(E0 + ["dice"], "7")], [], "does not match"),
    ("hp_pool_damage_not_dice", SLEEP, [(E0 + ["damage"], {"dice": "5d8", "type": "force"})], [], "'damage' is not one of"),
    ("hp_pool_needs_condition", SLEEP, [], [E0 + ["condition"]], "'condition' is a required property"),
    ("hp_pool_ends_on_damage_int", SLEEP, [(E0 + ["ends_on_damage"], 1)], [], "1 is not of type 'boolean'"),
    ("hp_pool_excludes_empty", SLEEP, [(E0 + ["excludes_creature_type"], [])], [], "should be non-empty"),
    ("hp_pool_excludes_bad", SLEEP, [(E0 + ["excludes_creature_type"], ["ghost"])], [], "'ghost' is not one of"),
    ("hp_pool_instantaneous", SLEEP, [(["duration"], {"kind": "instantaneous"})], [], "hp_pool needs a rounds, minutes or hours duration"),
    ("hp_pool_targets", SLEEP, [(["targets"], {"count": 2})], [], "targets not allowed on a hp_pool effect"),
    ("hp_pool_as_extra", ACID, [(E1, {"kind": "hp_pool", "dice": "1d8", "condition": "blinded"})], [], "effects[1]: hp_pool is only allowed as the first effect"),
    # dispel
    ("dispel_bare_dc_base", DISPEL, [(E0 + ["dc_base"], 10)], [], "'dc_base' is not one of"),
    ("dispel_dc_base_zero", DISPEL, [(E0 + ["check", "dc_base"], 0)], [], "0 is less than the minimum of 1"),
    ("dispel_dc_base_100", DISPEL, [(E0 + ["check", "dc_base"], 100)], [], "100 is greater than the maximum of 99"),
    ("dispel_check_ability", DISPEL, [(E0 + ["check", "ability"], "int")], [], "'spellcasting' was expected"),
    ("dispel_check_partial", DISPEL, [], [E0 + ["check", "plus"]], "'plus' is a required property"),
    ("dispel_upcast_dice", DISPEL, [(["scaling"], {"upcast": {"dice": 1}})], [], "scaling.upcast.dice not allowed on a dispel effect"),
    ("dispel_as_extra", ACID, [(E1, {"kind": "dispel"})], [], "effects[1]: dispel is only allowed as the first effect"),
    # revive
    ("revive_within_zero", REVIVE, [(E0 + ["within_rounds"], 0)], [], "0 is less than the minimum of 1"),
    ("revive_within_256", REVIVE, [(E0 + ["within_rounds"], 256)], [], "256 is greater than the maximum of 255"),
    ("revive_within_float", REVIVE, [(E0 + ["within_rounds"], 10.0)], [], "float value 10.0 not allowed"),
    ("revive_as_extra", ACID, [(E1, {"kind": "revive"})], [], "effects[1]: revive is only allowed as the first effect"),
    # end_condition
    ("end_condition_empty", LR, [(E0 + ["conditions"], [])], [], "should be non-empty"),
    ("end_condition_disease", LR, [(E0 + ["conditions"], ["disease"])], [], "'disease' is not one of"),
    ("end_condition_single_key", LR, [(E0 + ["condition"], "blinded")], [], "'condition' is not one of"),
    ("end_condition_as_extra", ACID, [(E1, {"kind": "end_condition", "conditions": ["blinded"]})], [], "effects[1]: end_condition is only allowed as the first effect"),
    # delayed_damage
    ("delayed_needs_expiry", ACID, [], [E1 + ["expiry"]], "'expiry' is a required property"),
    ("delayed_rounds_needs_n", ACID, [(E1 + ["expiry"], {"kind": "rounds"})], [], "'n' is a required property"),
    ("delayed_rounds_n_zero", ACID, [(E1 + ["expiry"], {"kind": "rounds", "n": 0})], [], "0 is less than the minimum of 1"),
    ("delayed_rounds_n_big", ACID, [(E1 + ["expiry"], {"kind": "rounds", "n": 65536})], [], "65536 is greater than the maximum of 65535"),
    ("delayed_next_turn_rejects_n", ACID, [(E1 + ["expiry"], {"kind": "until_end_of_target_next_turn", "n": 1})], [], "'n' is not one of ['kind']"),
    ("delayed_bad_expiry_kind", ACID, [(E1 + ["expiry"], {"kind": "concentration"})], [], "'concentration' is not one of"),
    ("delayed_needs_damage", ACID, [], [E1 + ["damage"]], "'damage' is a required property"),
    # sustained_damage
    ("sustained_rejects_expiry", HEAT, [(E0 + ["expiry"], {"kind": "rounds", "n": 1})], [], "'expiry' is not one of"),
    ("sustained_needs_damage", HEAT, [], [E0 + ["damage"]], "'damage' is a required property"),
    ("sustained_until_dispelled", HEAT, [(["duration"], {"kind": "until_dispelled", "concentration": True})], [], "sustained_damage needs a rounds, minutes or hours duration"),
    ("sustained_as_extra", ACID, [(E1, {"kind": "sustained_damage", "damage": {"dice": "1d8", "type": "fire"}})], [], "effects[1]: sustained_damage is only allowed as the first effect"),
    ("applies_to_on_first_only_kind", HEAT, [(E0 + ["applies_to"], "all")], [], "'applies_to' is not one of"),
]


def kinds_fixture(name: str) -> dict:
    return json.loads((KINDS_FIXTURES / f"{name}.json").read_text(encoding="utf-8"))


class SpellKindsR120(unittest.TestCase):
    """R-G15 spell@1 kinds (sim #120): positive fixtures (tests/validate/fixtures/spell_kinds) and the table above."""

    @classmethod
    def setUpClass(cls):
        cls.registry, cls.by_tag = vp.load_registry()
        cls.attribution = json.loads((CORE / "manifest.json").read_text(encoding="utf-8"))["attribution"]

    def run_docs(self, docs: dict[str, dict]):
        with tempfile.TemporaryDirectory(prefix="kinds_pack_") as tmp:
            pack = Path(tmp) / "test.kinds"
            (pack / "spells").mkdir(parents=True)
            for name, d in docs.items():
                (pack / "spells" / f"{name}.json").write_text(json.dumps(d, indent=2), encoding="utf-8")
            man = {"$schema": "manifest@1", "id": "test.kinds", "name": "spell kinds test", "version": "1.0.0",
                   "engine_min": "0.1.0", "feature_level": 1, "api_level": 1, "rules_profile": "srd51", "requires": [],
                   "optional": [], "depends": [], "attribution": self.attribution,
                   "contents": sorted(f"spells/{n}.json" for n in docs)}
            (pack / "manifest.json").write_text(json.dumps(man, indent=2), encoding="utf-8")
            warns = []
            errs = vp.validate_pack(pack, self.registry, self.by_tag, warns, vp.packset.PackSet([pack]))
            return errs, warns

    def test_positive_fixtures_validate_clean(self):
        fx = {f.stem: kinds_fixture(f.stem) for f in sorted(KINDS_FIXTURES.glob("*.json"))}
        self.assertEqual(sorted(fx), sorted([SLEEP, SPRAY, DISPEL, REVIVE, LR, ACID, HEAT]))
        self.assertEqual(self.run_docs(fx), ([], []))

    def test_optional_keys_and_defaults_accepted(self):
        # dispel without check (dc_base 10), revive without within_rounds, hp_pool minimal, delayed_damage alone with rounds.
        docs = {DISPEL: _edit(kinds_fixture(DISPEL), [], [E0 + ["check"]]),
                REVIVE: _edit(kinds_fixture(REVIVE), [], [E0 + ["within_rounds"]]),
                SPRAY: _edit(kinds_fixture(SPRAY), [], [["scaling"]]),
                "later": _edit(kinds_fixture(ACID), [(["id"], "test.kinds.spell.later"),
                               (["effects"], [{"kind": "delayed_damage", "damage": {"dice": "2d4", "type": "acid"},
                                               "expiry": {"kind": "rounds", "n": 2}}])], [])}
        self.assertEqual(self.run_docs(docs), ([], []))

    def test_cases(self):
        for name, fx, sets, dels, expect in KIND_CASES:
            with self.subTest(name):
                errs, _ = self.run_docs({fx: _edit(kinds_fixture(fx), sets, dels)})
                self.assertTrue(any(expect in e for e in errs), (expect, errs))


def _edit(doc: dict, sets, dels) -> dict:
    import copy
    d = copy.deepcopy(doc)
    for path, value in sets:
        parent = d
        for k in path[:-1]:
            parent = parent[k]
        if isinstance(parent, list) and path[-1] == len(parent):
            parent.append(value)
        else:
            parent[path[-1]] = value
    for path in dels:
        parent = d
        for k in path[:-1]:
            parent = parent[k]
        del parent[path[-1]]
    return d


if __name__ == "__main__":
    unittest.main()
