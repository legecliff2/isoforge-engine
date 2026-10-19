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


ZONE_FIXTURES = vp.ROOT / "tests" / "validate" / "fixtures" / "zones" / "spells"
MB, SG, FOG, SPIKE, WEB, GREASE = "moonbeam", "spirit_guardians", "fog_cloud", "spike_growth", "web", "grease"
T0 = ["effects", 0, "zone", "triggers", 0]
SAVE_FX = {"kind": "save", "ability": "con", "damage": {"dice": "2d10", "type": "radiant"}, "on_success": "half"}
# R-G11 spell@1 kind `zone` (docs/CONTENT_CONVENTIONS.md section 4). (name, fixture, sets, dels, expected substring).
ZONE_CASES = [
    # schema: closed objects, enums, integer ranges, shape arms
    ("bad_on", MB, [(T0 + ["on"], "enter")], [], "'enter' is not one of"),
    ("bad_flag", FOG, [(["effects", 0, "zone", "flags"], ["fog"])], [], "'fog' is not one of"),
    ("flags_raw_bitmask", FOG, [(["effects", 0, "zone", "flags"], 4)], [], "is not of type 'array'"),
    ("flags_duplicate", FOG, [(["effects", 0, "zone", "flags"], ["difficult", "difficult"])], [], "has non-unique elements"),
    ("bad_anchor", FOG, [(["effects", 0, "zone", "anchor"], "object")], [], "'object' is not one of"),
    ("shape_line_not_yet", FOG, [(["effects", 0, "zone", "shape"], "line")], [], "'line' is not one of"),
    ("cylinder_needs_height", MB, [], [["effects", 0, "zone", "height_ft"]], "'height_ft' is a required property"),
    ("sphere_rejects_height", FOG, [(["effects", 0, "zone", "height_ft"], 10)], [], "'height_ft' is not one of"),
    ("size_not_multiple_of_5", FOG, [(["effects", 0, "zone", "size_ft"], 12)], [], "is not a multiple of 5"),
    ("size_float", FOG, [(["effects", 0, "zone", "size_ft"], 20.0)], [], "float value 20.0 not allowed"),
    ("size_above_u16", FOG, [(["effects", 0, "zone", "size_ft"], 65535)], [], "is greater than the maximum of 65530"),
    ("move_bad_cost", MB, [(["effects", 0, "zone", "move", "cost"], "reaction")], [], "'reaction' is not one of"),
    ("move_needs_feet", MB, [], [["effects", 0, "zone", "move", "feet"]], "'feet' is a required property"),
    ("group_above_255", MB, [(T0 + ["group"], 256)], [], "is greater than the maximum of 255"),
    ("group_negative", MB, [(T0 + ["group"], -1)], [], "is less than the minimum of 0"),
    ("needs_conscious_owner_not_bool", SG, [(["effects", 0, "zone", "needs_conscious_owner"], 1)], [], "1 is not of type 'boolean'"),
    ("needs_conscious_owner_string", SG, [(["effects", 0, "zone", "needs_conscious_owner"], "yes")], [], "'yes' is not of type 'boolean'"),
    ("unknown_zone_key", MB, [(["effects", 0, "zone", "move_cost"], 1)], [], "'move_cost' is not one of"),
    ("unknown_trigger_key", MB, [(T0 + ["limit"], "none")], [], "Additional properties are not allowed ('limit'"),
    ("legacy_trigger_key", MB, [(T0 + ["trigger"], "zone_enter")], [], "Additional properties are not allowed ('trigger'"),
    ("trigger_effect_spell_attack", MB, [(T0 + ["effect"], {"kind": "spell_attack", "attack": "ranged", "damage": {"dice": "1d6", "type": "fire"}})], [], "'spell_attack' is not one of"),
    ("trigger_effect_zone", MB, [(T0 + ["effect"], {"kind": "zone"})], [], "'zone' is not one of"),
    ("trigger_save_repeat_save", MB, [(T0 + ["effect", "repeat_save"], "end_of_turn")], [], "'repeat_save' is not one of"),
    ("trigger_save_needs_on_success", MB, [], [T0 + ["effect", "on_success"]], "'on_success' is a dependency of 'damage'"),
    ("trigger_damage_needs_damage", SPIKE, [(T0 + ["effect"], {"kind": "damage"})], [], "'damage' is a required property"),
    ("trigger_apply_condition_no_damage", WEB, [(T0 + ["effect"], {"kind": "apply_condition", "condition": "restrained", "damage": {"dice": "1d4", "type": "fire"}})], [], "'damage' is not one of"),
    ("triggers_empty", MB, [(["effects", 0, "zone", "triggers"], [])], [], "should be non-empty"),
    ("zone_applies_to", MB, [(["effects", 0, "applies_to"], "all")], [], "Additional properties are not allowed ('applies_to'"),
    # validator (check_spell / check_spell_zone): the loader contract
    ("zone_as_extra_effect", "fog_cloud", [(["effects", 1], {"kind": "zone", "zone": {"shape": "sphere", "size_ft": 5, "anchor": "point", "flags": ["difficult"]}})], [], "zone must be the primary effect (effects[1] is a zone)"),
    ("zone_with_area", FOG, [(["area"], {"shape": "sphere", "size_ft": 20})], [], "zone: area not allowed"),
    ("zone_with_targets", FOG, [(["targets"], {"count": 1})], [], "targets not allowed on a zone effect"),
    ("zone_instantaneous", FOG, [(["duration"], {"kind": "instantaneous"})], [], "zone: needs a lasting duration"),
    ("aura_needs_range_self", SG, [(["range"], {"kind": "ranged", "feet": 30})], [], "zone: anchor creature (an aura on the caster) needs range self"),
    ("move_on_aura", SG, [(["effects", 0, "zone", "move"], {"cost": "action", "feet": 30})], [], "zone: move needs anchor point"),
    ("obscured_exclusive", FOG, [(["effects", 0, "zone", "flags"], ["light_obscured", "heavy_obscured"])], [], "zone: flags light_obscured and heavy_obscured are exclusive"),
    ("zone_does_nothing", FOG, [], [["effects", 0, "zone", "flags"]], "zone: needs triggers or flags"),
    ("own_turn_only_on_start_turn", WEB, [(T0 + ["own_turn_only"], True)], [], "zone.triggers[0]: own_turn_only is only for zone_enter / zone_move_within"),
    ("aura_move_enter_on_start_turn", MB, [(["effects", 0, "zone", "triggers", 1, "counts_aura_move_as_enter"], True)], [], "zone.triggers[1]: counts_aura_move_as_enter is only for zone_enter"),
    ("aura_move_enter_never_moves", WEB, [(["effects", 0, "zone", "triggers", 1, "counts_aura_move_as_enter"], True)], [], "zone.triggers[1]: counts_aura_move_as_enter on a zone that never moves"),
    ("while_in_zone_needs_condition", MB, [(T0 + ["while_in_zone"], True)], [], "zone.triggers[0]: while_in_zone needs an effect condition"),
    ("upcast_dice_needs_damage", WEB, [(["scaling"], {"upcast": {"dice": 1}})], [], "scaling.upcast.dice needs effect dice or damage"),
    ("upcast_targets_on_zone", MB, [(["scaling", "upcast", "targets"], 1)], [], "scaling.upcast.targets not allowed on a zone effect"),
    ("zone_as_reaction", FOG, [(["casting_time"], {"kind": "reaction", "trigger": {"kind": "being_hit"}})], [], "effect kind zone and casting_time.kind reaction disagree"),
]


def zone_fixture(name: str) -> dict:
    return json.loads((ZONE_FIXTURES / f"{name}.json").read_text(encoding="utf-8"))


def zone_edit(doc: dict, sets, dels) -> dict:
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


class SpellZones(unittest.TestCase):
    """R-G11 spell@1 kind `zone`: positive fixtures (tests/validate/fixtures/zones) and the table above."""

    @classmethod
    def setUpClass(cls):
        cls.registry, cls.by_tag = vp.load_registry()
        cls.attribution = json.loads((CORE / "manifest.json").read_text(encoding="utf-8"))["attribution"]

    def run_docs(self, docs: dict[str, dict]):
        with tempfile.TemporaryDirectory(prefix="zone_pack_") as tmp:
            pack = Path(tmp) / "test.zone"
            (pack / "spells").mkdir(parents=True)
            for name, d in docs.items():
                (pack / "spells" / f"{name}.json").write_text(json.dumps(d, indent=2), encoding="utf-8")
            man = {"$schema": "manifest@1", "id": "test.zone", "name": "zone test", "version": "1.0.0", "engine_min": "0.1.0",
                   "feature_level": 1, "api_level": 1, "rules_profile": "srd51", "requires": [], "optional": [], "depends": [],
                   "attribution": self.attribution, "contents": sorted(f"spells/{n}.json" for n in docs)}
            (pack / "manifest.json").write_text(json.dumps(man, indent=2), encoding="utf-8")
            warns = []
            errs = vp.validate_pack(pack, self.registry, self.by_tag, warns, vp.packset.PackSet([pack]))
            return errs, warns

    def all_fixtures(self) -> dict[str, dict]:
        return {f.stem: zone_fixture(f.stem) for f in sorted(ZONE_FIXTURES.glob("*.json"))}

    def test_positive_fixtures_validate_clean(self):
        fx = self.all_fixtures()
        self.assertEqual(sorted(fx), sorted(["darkness", FOG, GREASE, MB, "silence", SPIKE, SG, WEB]))
        self.assertEqual(self.run_docs(fx), ([], []))

    def test_cases(self):
        for name, fx, sets, dels, expect in ZONE_CASES:
            with self.subTest(name):
                errs, _ = self.run_docs({fx: zone_edit(zone_fixture(fx), sets, dels)})
                self.assertTrue(any(expect in e for e in errs), (expect, errs))

    def test_optional_keys_and_defaults_accepted(self):
        # group 0..255, every boolean, bonus_action moves, the no-save condition kind, a cube aura.
        mb = zone_edit(zone_fixture(MB), [(T0 + ["group"], 255), (T0 + ["completely_within"], True),
                                          (T0 + ["counts_aura_move_as_enter"], True),
                                          (["effects", 0, "zone", "move", "cost"], "bonus_action"), (["effects", 0, "zone", "needs_conscious_owner"], False),
                                          (["effects", 0, "zone", "triggers", 2], {"on": "zone_end_turn",
                                           "effect": {"kind": "apply_condition", "condition": "blinded"}, "while_in_zone": True})], [])
        sg = zone_edit(zone_fixture(SG), [(["effects", 0, "zone", "shape"], "cube"),
                                          (["effects", 0, "zone", "triggers", 0, "counts_aura_move_as_enter"], True)], [])
        self.assertEqual(self.run_docs({MB: mb, SG: sg}), ([], []))

    def test_messages_avoid_the_loader_compared_prefixes(self):
        # spell_loader_tests compares a negative case's message with the loader's only for these prefixes; the loader does not
        # parse zones yet, so zone messages must not start with one (tests/validate/cases zone_*.expect included).
        prefixes = ("scaling.", "effect", "targets not", "ac_bonus", "casting_time", "disagree", "is not a spell@1", "'", "until_start")
        for f in sorted((vp.ROOT / "tests" / "validate" / "cases").glob("zone_*.expect")):
            with self.subTest(f.name):
                self.assertFalse(f.read_text(encoding="utf-8").strip().startswith(prefixes))


if __name__ == "__main__":
    unittest.main()
