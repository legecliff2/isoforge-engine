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
# R-G11 rules PR 2 positives kept apart so spell_loader_tests' 8-spell zone fixture book is unchanged (CONTENT_CONVENTIONS 4.5)
ZONE_FIXTURES_PR2 = ZONE_FIXTURES.parent / "spells_pr2"
MB, SG, FOG, SPIKE, WEB, GREASE = "moonbeam", "spirit_guardians", "fog_cloud", "spike_growth", "web", "grease"
SC, WOF, AURA = "stinking_cloud", "wall_of_fire", "warding_aura"   # R-G11 rules PR 2 keys (CONTENT_CONVENTIONS 4.6)
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
    ("line_needs_length", FOG, [(["effects", 0, "zone", "shape"], "line")], [["effects", 0, "zone", "size_ft"]], "'length_ft' is a required property"),
    ("line_rejects_size", WOF, [(["effects", 0, "zone", "size_ft"], 20)], [], "'size_ft' is not one of"),
    ("line_needs_height", WOF, [], [["effects", 0, "zone", "height_ft"]], "'height_ft' is a required property"),
    ("sphere_needs_size", FOG, [], [["effects", 0, "zone", "size_ft"]], "'size_ft' is a required property"),
    ("sphere_rejects_length", FOG, [(["effects", 0, "zone", "length_ft"], 20)], [], "'length_ft' is not one of"),
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
    # rules PR 2 (#122) ZoneSpec / ZoneTriggerRow fields its loader still refuses ("zone: unknown key"); see CONTENT_CONVENTIONS 4.5
    # R-G11 rules PR 2 keys (CONTENT_CONVENTIONS 4.6): schema types / ranges / shape arms
    ("upcast_size_not_multiple_of_5", FOG, [(["effects", 0, "zone", "upcast_size_ft"], 12)], [], "is not a multiple of 5"),
    ("upcast_size_float", FOG, [(["effects", 0, "zone", "upcast_size_ft"], 20.0)], [], "float value 20.0 not allowed"),
    ("upcast_size_on_line", WOF, [(["effects", 0, "zone", "upcast_size_ft"], 20)], [], "'upcast_size_ft' is not one of"),
    ("ring_size_on_sphere", FOG, [(["effects", 0, "zone", "ring_size_ft"], 20)], [], "'ring_size_ft' is not one of"),
    ("sided_on_sphere", FOG, [(["effects", 0, "zone", "sided"], True)], [], "'sided' is not one of"),
    ("sided_not_bool", WOF, [(["effects", 0, "zone", "sided"], 1)], [], "1 is not of type 'boolean'"),
    ("side_ft_below_5", WOF, [(["effects", 0, "zone", "side_ft"], 0)], [], "is less than the minimum of 5"),
    ("length_above_u16", WOF, [(["effects", 0, "zone", "length_ft"], 65535)], [], "is greater than the maximum of 65530"),
    ("aura_modifier_unknown_key", AURA, [(["effects", 0, "zone", "aura_modifier", "duration"], {"kind": "rounds", "n": 1})], [], "Additional properties are not allowed ('duration'"),
    ("aura_modifier_bad_stat", AURA, [(["effects", 0, "zone", "aura_modifier", "stat"], "luck")], [], "'luck' is not one of"),
    ("aura_modifier_bonus_above_30", AURA, [(["effects", 0, "zone", "aura_modifier", "bonus"], 31)], [], "is greater than the maximum of 30"),
    ("aura_bonus_ability_bad", AURA, [(["effects", 0, "zone", "aura_bonus_ability"], "luck")], [], "'luck' is not one of"),
    ("aura_immunity_empty", AURA, [(["effects", 0, "zone", "aura_immunity"], [])], [], "should be non-empty"),
    ("aura_immunity_duplicate", AURA, [(["effects", 0, "zone", "aura_immunity"], ["frightened", "frightened"])], [], "has non-unique elements"),
    ("aura_immunity_bad", AURA, [(["effects", 0, "zone", "aura_immunity"], ["scared"])], [], "'scared' is not one of"),
    ("aura_allies_only_not_bool", AURA, [(["effects", 0, "zone", "aura_allies_only"], "yes")], [], "'yes' is not of type 'boolean'"),
    ("region_bad", WOF, [(["effects", 0, "zone", "triggers", 1, "region"], "side")], [], "'side' is not one of"),
    ("loses_action_not_bool", SC, [(T0 + ["loses_action"], 1)], [], "1 is not of type 'boolean'"),
    ("auto_success_damage_empty", SC, [(T0 + ["auto_success_damage"], [])], [], "should be non-empty"),
    ("auto_success_damage_bad", SC, [(T0 + ["auto_success_damage"], ["gas"])], [], "'gas' is not one of"),
    ("auto_success_conditions_duplicate", SC, [(T0 + ["auto_success_conditions"], ["poisoned", "poisoned"])], [], "has non-unique elements"),
    ("auto_success_no_breath_not_bool", SC, [(T0 + ["auto_success_no_breath"], "true")], [], "'true' is not of type 'boolean'"),
    ("shapechanger_disadvantage_not_bool", MB, [(T0 + ["shapechanger_disadvantage"], 0)], [], "0 is not of type 'boolean'"),
    ("evil_damage_type_bad", SG, [(T0 + ["evil_damage_type"], "unholy")], [], "'unholy' is not one of"),
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
    # R-G11 rules PR 2 cross-field rules (validator check_spell_zone; loader parse_zone, same wording)
    ("side_ft_without_sided", WOF, [(["effects", 0, "zone", "sided"], False)], [], "zone: side_ft is required for (and only for) sided zones"),
    ("sided_without_side_ft", WOF, [], [["effects", 0, "zone", "side_ft"]], "zone: side_ft is required for (and only for) sided zones"),
    ("aura_bonus_ability_without_modifier", AURA, [], [["effects", 0, "zone", "aura_modifier"]], "zone: aura_bonus_ability needs aura_modifier"),
    ("aura_modifier_not_save", AURA, [(["effects", 0, "zone", "aura_modifier", "stat"], "ability_check")], [], "zone.aura_modifier stat must be save"),
    ("aura_fields_on_point_zone", FOG, [(["effects", 0, "zone", "aura_allies_only"], True)], [], "zone: aura fields need anchor creature"),
    ("aura_immunity_on_point_zone", FOG, [(["effects", 0, "zone", "aura_immunity"], ["frightened"])], [], "zone: aura fields need anchor creature"),
    ("region_side_unsided", MB, [(["effects", 0, "zone", "triggers", 1, "region"], "zone_or_side")], [], "zone.triggers[1]: region zone_or_side needs a sided zone"),
    ("region_side_on_enter", WOF, [(["effects", 0, "zone", "triggers", 2, "region"], "zone_or_side")], [], "zone.triggers[2]: region zone_or_side is only for zone_appear / zone_start_turn / zone_end_turn"),
    ("auto_success_on_damage_row", WOF, [(["effects", 0, "zone", "triggers", 1, "auto_success_damage"], ["fire"])], [], "zone.triggers[1]: auto_success / shapechanger_disadvantage need effect kind save"),
    ("shapechanger_on_damage_row", SPIKE, [(T0 + ["shapechanger_disadvantage"], True)], [], "zone.triggers[0]: auto_success / shapechanger_disadvantage need effect kind save"),
    ("evil_damage_type_without_damage", SC, [(T0 + ["evil_damage_type"], "necrotic")], [], "zone.triggers[0]: evil_damage_type needs effect damage"),
    ("zone_as_reaction", FOG, [(["casting_time"], {"kind": "reaction", "trigger": {"kind": "being_hit"}})], [], "effect kind zone and casting_time.kind reaction disagree"),
]


def zone_fixture(name: str) -> dict:
    f = ZONE_FIXTURES / f"{name}.json"
    return json.loads((f if f.exists() else ZONE_FIXTURES_PR2 / f"{name}.json").read_text(encoding="utf-8"))


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
        return {f.stem: zone_fixture(f.stem) for f in sorted([*ZONE_FIXTURES.glob("*.json"), *ZONE_FIXTURES_PR2.glob("*.json")])}

    def test_positive_fixtures_validate_clean(self):
        fx = self.all_fixtures()
        self.assertEqual(sorted(fx), sorted(["darkness", FOG, GREASE, MB, "silence", SPIKE, SG, WEB, SC, WOF, AURA]))
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


KINDS_FIXTURES = vp.ROOT / "tests" / "validate" / "fixtures" / "spell_kinds" / "spells"
SLEEP, SPRAY, DISPEL, REVIVE, LR, ACID, HEAT = ("sleep", "color_spray", "dispel_magic", "revivify", "lesser_restoration",
                                                "acid_arrow", "heat_metal")
E0, E1 = ["effects", 0], ["effects", 1]
# R-G15 spell@1 kinds wired by sim #120 (docs/CONTENT_CONVENTIONS.md section 6). (name, fixture, sets, dels, expected substring).
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

    def test_priced_material_item(self):
        # components.m.item (priced materials, docs/CONTENT_CONVENTIONS.md section 7): no item@1 document is required.
        m = ["components", "m"]
        rv = kinds_fixture(REVIVE)
        self.assertEqual(rv["components"]["m"]["item"], "srd51.item.diamond")
        self.assertEqual(self.run_docs({REVIVE: rv}), ([], []))
        errs, warns = self.run_docs({REVIVE: _edit(rv, [], [m + ["item"]])})
        self.assertEqual(errs, [])
        self.assertEqual(len(warns), 1, warns)
        self.assertIn("/components/m: cost_cp 30000 without item", warns[0])
        errs, warns = self.run_docs({REVIVE: _edit(rv, [], [m + ["cost_cp"], m + ["consumed"]])})
        self.assertEqual(errs, [])
        self.assertTrue(any("item 'srd51.item.diamond' is ignored without cost_cp" in w for w in warns), warns)
        # 7.2zd follow-up: consumed no longer requires cost_cp (Protection from Evil and Good: holy water or powdered silver
        # and iron, consumed, no listed cost). Positive: no error and no warning beyond the unpriced-material ones.
        pfeg = _edit(rv, [(m, {"text": "holy water or powdered silver and iron, which the spell consumes", "consumed": True})], [])
        errs, warns = self.run_docs({REVIVE: pfeg})
        self.assertEqual(errs, [])
        self.assertFalse(any("/components/m" in w for w in warns), warns)
        for bad, want in (("Diamond", "does not match"), ("diamond", "does not match"), (7, "is not of type 'string'")):
            with self.subTest(bad):
                errs, _ = self.run_docs({REVIVE: _edit(rv, [(m + ["item"], bad)], [])})
                self.assertTrue(any(want in e for e in errs), (want, errs))
        # an unrelated key is still refused (components.m stays closed)
        errs, _ = self.run_docs({REVIVE: _edit(rv, [(m + ["value_gp"], 300)], [])})
        self.assertTrue(any("Additional properties are not allowed ('value_gp'" in e for e in errs), errs)
        # a free material with no item needs nothing (no warning)
        self.assertEqual(self.run_docs({LR: _edit(kinds_fixture(LR), [(["components", "m"], {"text": "a feather"})], [])}), ([], []))

    def test_cases(self):
        for name, fx, sets, dels, expect in KIND_CASES:
            with self.subTest(name):
                errs, _ = self.run_docs({fx: _edit(kinds_fixture(fx), sets, dels)})
                self.assertTrue(any(expect in e for e in errs), (expect, errs))


FORCED_FIXTURES = vp.ROOT / "tests" / "validate" / "fixtures" / "spell_forced" / "spells"
MISTY, SHOVE = "misty_step", "shove_bolt"
# R-G9 spell@1 kinds forced_move / teleport (sim #144, spell_loader.cpp). (name, fixture, sets, dels, expected substring).
# Cross-field messages are the loader's verbatim; shape errors are the schema's.
FORCED_CASES = [
    ("forced_needs_feet", SHOVE, [], [E1 + ["feet"]], "'feet' is a required property"),
    ("forced_feet_not_5", SHOVE, [(E1 + ["feet"], 7)], [], "7 is not a multiple of 5"),
    ("forced_feet_zero", SHOVE, [(E1 + ["feet"], 0)], [], "0 is less than the minimum of 5"),
    ("forced_feet_float", SHOVE, [(E1 + ["feet"], 10.0)], [], "float value 10.0 not allowed"),
    ("forced_mode_line", SHOVE, [(E1 + ["mode"], "line")], [], "'line' is not one of ['push', 'pull']"),
    ("forced_max_size_bad", SHOVE, [(E1 + ["max_size"], "colossal")], [], "'colossal' is not one of"),
    ("forced_provokes_oa", SHOVE, [(E1 + ["provokes_oa"], True)], [], "False was expected"),
    ("forced_up_to_int", SHOVE, [(E1 + ["up_to"], 1)], [], "1 is not of type 'boolean'"),
    ("forced_unknown_key", SHOVE, [(E1 + ["distance"], 10)], [], "'distance' is not one of"),
    ("teleport_needs_max_feet", MISTY, [], [E0 + ["max_feet"]], "'max_feet' is a required property"),
    ("teleport_max_feet_not_5", MISTY, [(E0 + ["max_feet"], 32)], [], "32 is not a multiple of 5"),
    ("teleport_needs_sight_int", MISTY, [(E0 + ["needs_sight"], 0)], [], "0 is not of type 'boolean'"),
    ("teleport_applies_to", MISTY, [(E0 + ["applies_to"], "all")], [], "'applies_to' is not one of"),
    ("teleport_as_extra", SHOVE, [(E1, {"kind": "teleport", "max_feet": 30})], [], "effects[1]: teleport is only allowed as the first effect"),
    ("forced_targets", SHOVE, [(E0, {"kind": "forced_move", "feet": 10}), (["targets"], {"count": 2})], [],
     "targets not allowed on a forced_move effect"),
]


class SpellForcedRG9(unittest.TestCase):
    """R-G9 spell@1 kinds (sim #144): positive fixtures (tests/validate/fixtures/spell_forced) and FORCED_CASES."""

    @classmethod
    def setUpClass(cls):
        cls.registry, cls.by_tag = vp.load_registry()
        cls.attribution = json.loads((CORE / "manifest.json").read_text(encoding="utf-8"))["attribution"]

    run_docs = SpellKindsR120.run_docs

    def fx(self, name):
        return json.loads((FORCED_FIXTURES / f"{name}.json").read_text(encoding="utf-8"))

    def test_positive_fixtures_validate_clean(self):
        self.assertEqual(self.run_docs({MISTY: self.fx(MISTY), SHOVE: self.fx(SHOVE)}), ([], []))

    def test_optional_keys_accepted(self):
        full = _edit(self.fx(SHOVE), [(E1 + ["mode"], "pull"), (E1 + ["up_to"], True), (E1 + ["max_size"], "large"),
                                      (E1 + ["provokes_oa"], False)], [E1 + ["applies_to"]])
        sight = _edit(self.fx(MISTY), [(E0 + ["needs_sight"], False)], [])
        self.assertEqual(self.run_docs({SHOVE: full, MISTY: sight}), ([], []))

    def test_cases(self):
        for name, fx, sets, dels, expect in FORCED_CASES:
            with self.subTest(name):
                errs, _ = self.run_docs({fx: _edit(self.fx(fx), sets, dels)})
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


SURPRISE_FIXTURES = vp.ROOT / "tests" / "validate" / "fixtures" / "surprise" / "chunks"
SIDES, SPAWNS = "ambush_sides", "ambush_spawns"
# R-G16 chunk@1 `surprise` (docs/CONTENT_CONVENTIONS.md section 8). (name, fixture, sets, dels, expected substring).
SURPRISE_CASES = [
    # schema: closed object, exactly one of sides / spawns, names and indices
    ("both_keys", SIDES, [(["surprise", "spawns"], [0])], [], "is valid under each of"),
    ("neither_key", SIDES, [(["surprise"], {})], [], "is not valid under any of the given schemas"),
    ("unknown_key", SIDES, [(["surprise", "rounds"], 1)], [], "Additional properties are not allowed ('rounds'"),
    ("not_an_object", SIDES, [(["surprise"], ["party"])], [], "is not of type 'object'"),
    ("sides_empty", SIDES, [(["surprise", "sides"], [])], [], "should be non-empty"),
    ("sides_duplicate", SIDES, [(["surprise", "sides"], ["party", "party"])], [], "has non-unique elements"),
    ("side_bad_name", SIDES, [(["surprise", "sides"], ["Party"])], [], "does not match"),
    ("side_not_string", SIDES, [(["surprise", "sides"], [1])], [], "is not of type 'string'"),
    ("spawns_empty", SPAWNS, [(["surprise", "spawns"], [])], [], "should be non-empty"),
    ("spawns_duplicate", SPAWNS, [(["surprise", "spawns"], [0, 0])], [], "has non-unique elements"),
    ("spawn_negative", SPAWNS, [(["surprise", "spawns"], [-1])], [], "is less than the minimum of 0"),
    ("spawn_float", SPAWNS, [(["surprise", "spawns"], [1.0])], [], "float value 1.0 not allowed"),
    ("spawn_string", SPAWNS, [(["surprise", "spawns"], ["0"])], [], "is not of type 'integer'"),
    ("spawn_stable_id_object", SPAWNS, [(["surprise", "spawns"], [{"stable_id": 1}])], [], "is not of type 'integer'"),
    # validator (check_chunk_surprise): the refs the future loader resolves
    ("spawn_index_out_of_range", SPAWNS, [(["surprise", "spawns"], [4])], [], "surprise.spawns[0]: index 4 is not a spawns[] index (this chunk has 4 spawns)"),
    ("spawn_not_a_combatant", SPAWNS, [(["spawns", 4], {"archetype": "srd51.spell.fire_bolt", "tile": {"x": 12, "y": 15}}), (["surprise", "spawns"], [0, 4])], [],
     "surprise.spawns[1]: spawns[4] (srd51.spell.fire_bolt, a spell) is not a combatant"),
    ("side_without_members", SIDES, [(["surprise", "sides"], ["party", "bandits"])], [], "surprise.sides[1]: no combatant spawn in this chunk is on team"),
    ("side_default_team_overridden", SPAWNS, [(["surprise"], {"sides": ["monsters"]})], [], "surprise.sides[0]: no combatant spawn in this chunk is on team"),
]


def surprise_fixture(name: str) -> dict:
    return json.loads((SURPRISE_FIXTURES / f"{name}.json").read_text(encoding="utf-8"))


class ChunkSurprise(unittest.TestCase):
    """R-G16 chunk@1 `surprise`: positive fixtures (tests/validate/fixtures/surprise) and the table above."""

    @classmethod
    def setUpClass(cls):
        cls.registry, cls.by_tag = vp.load_registry()
        cls.attribution = json.loads((CORE / "manifest.json").read_text(encoding="utf-8"))["attribution"]

    def run_docs(self, docs: dict[str, dict], rehash: bool = True):
        with tempfile.TemporaryDirectory(prefix="surprise_pack_") as tmp:
            pack = Path(tmp) / "test.surprise"
            (pack / "chunks").mkdir(parents=True)
            for name, d in docs.items():
                if rehash and "content_hash" in d:
                    d = {**d, "content_hash": vp.hash_hex(vp.compute_content_hash(d))}   # an edited spawns[] moves the hash
                (pack / "chunks" / f"{name}.json").write_text(json.dumps(d, indent=2), encoding="utf-8")
            man = {"$schema": "manifest@1", "id": "test.surprise", "name": "surprise test", "version": "1.0.0", "engine_min": "0.1.0",
                   "feature_level": 1, "api_level": 1, "rules_profile": "srd51", "requires": [], "optional": [],
                   "depends": [{"id": "srd51.core", "version": "^1.0"}], "attribution": self.attribution,
                   "contents": sorted(f"chunks/{n}.json" for n in docs)}
            (pack / "manifest.json").write_text(json.dumps(man, indent=2), encoding="utf-8")
            warns = []
            errs = vp.validate_pack(pack, self.registry, self.by_tag, warns, vp.packset.PackSet([CORE, pack]))
            return errs, warns

    def test_positive_fixtures_validate_with_the_reserved_warning_only(self):
        fx = {f.stem: surprise_fixture(f.stem) for f in sorted(SURPRISE_FIXTURES.glob("*.json"))}
        self.assertEqual(sorted(fx), [SIDES, SPAWNS])
        errs, warns = self.run_docs(fx, rehash=False)   # the committed content_hash values must already be right
        self.assertEqual(errs, [])
        self.assertEqual(sorted(warns), sorted(f"chunks/{n}.json: {vp.SURPRISE_RESERVED}" for n in fx))
        self.assertIn("has no effect until that loader lands", vp.SURPRISE_RESERVED)

    def test_cases(self):
        for name, fx, sets, dels, expect in SURPRISE_CASES:
            with self.subTest(name):
                errs, _ = self.run_docs({fx: zone_edit(surprise_fixture(fx), sets, dels)})
                self.assertTrue(any(expect in e for e in errs), (expect, errs))
                self.assertFalse(expect.startswith("'"), "expected substrings must not start with a quote")

    def test_surprise_is_not_hashed(self):
        # the chunk decoder (chunk_codec.cpp) ignores the key, so content_hash is the same with and without it
        for fx in (SIDES, SPAWNS):
            d = surprise_fixture(fx)
            bare = {k: v for k, v in d.items() if k != "surprise"}
            self.assertEqual(vp.compute_content_hash(d), vp.compute_content_hash(bare))
            self.assertEqual(d["content_hash"], vp.hash_hex(vp.compute_content_hash(bare)))
            errs, warns = self.run_docs({fx: bare}, rehash=False)
            self.assertEqual((errs, warns), ([], []))   # without the key: no warning (old content unchanged)

    def test_both_default_teams_and_overrides(self):
        # sides by default team ('party' = character archetypes) and by override ('bandits'); spawns by index
        d = zone_edit(surprise_fixture(SPAWNS), [(["surprise"], {"sides": ["bandits", "party"]})], [])
        errs, warns = self.run_docs({SPAWNS: d})
        self.assertEqual(errs, [])
        self.assertEqual(len(warns), 1)

    def test_no_shipped_or_compat_chunk_carries_surprise(self):
        # the key is new: nothing that exists today changes meaning when the loader lands
        for f in sorted(list((vp.ROOT / "content" / "packs").rglob("chunks/*.json")) + list((vp.ROOT / "tests" / "compat").rglob("chunks/*.json"))):
            with self.subTest(str(f.relative_to(vp.ROOT))):
                self.assertNotIn("surprise", json.loads(f.read_text(encoding="utf-8")))


class CreatureFacts(unittest.TestCase):
    """R-G11 creature facts no_breath / shapechanger / evil (CONTENT_CONVENTIONS 4.6): accepted on monster@4 / character@4,
    reserved (warning, no error) until the sim archetype loader (Engine Core) reads them."""

    def run_monster(self, doc):
        reg, by_tag = vp.load_registry()
        att = json.loads((CORE / "manifest.json").read_text(encoding="utf-8"))["attribution"]
        with tempfile.TemporaryDirectory(prefix="facts_pack_") as tmp:
            pack = Path(tmp) / "test.facts"
            (pack / "monsters").mkdir(parents=True)
            doc = dict(doc, id="test.facts.monster.axe_beak")
            (pack / "monsters" / "axe_beak.json").write_text(json.dumps(doc, indent=2), encoding="utf-8")
            man = {"$schema": "manifest@1", "id": "test.facts", "name": "facts", "version": "1.0.0", "engine_min": "0.1.0",
                   "feature_level": 1, "api_level": 2, "rules_profile": "srd51", "requires": [], "optional": [], "depends": [],
                   "attribution": att, "contents": ["monsters/axe_beak.json"]}
            (pack / "manifest.json").write_text(json.dumps(man, indent=2), encoding="utf-8")
            warns = []
            errs = vp.validate_pack(pack, reg, by_tag, warns, vp.packset.PackSet([pack]))
            return errs, warns

    def base(self):
        return json.loads((CORE / "monsters" / "axe_beak.json").read_text(encoding="utf-8"))

    def test_facts_accepted_with_reserved_warning(self):
        errs, warns = self.run_monster(dict(self.base(), no_breath=True, shapechanger=False, evil=True))
        self.assertEqual(errs, [])
        self.assertEqual(sorted(k for k in vp.CREATURE_FACTS if any(f"/{k}: {k} is reserved" in w for w in warns)),
                         sorted(vp.CREATURE_FACTS))

    def test_fact_not_boolean(self):
        errs, _ = self.run_monster(dict(self.base(), evil="yes"))
        self.assertTrue(any("'yes' is not of type 'boolean'" in e for e in errs), errs)


if __name__ == "__main__":
    unittest.main()


class ChunkFlagsMoveModes(unittest.TestCase):
    """feat/move-modes: chunk@1 flag bits 4 (climbable) and 5 (open_air) are accepted; bits 6-7 stay reserved."""

    run_docs = ChunkSurprise.run_docs

    @classmethod
    def setUpClass(cls):
        ChunkSurprise.setUpClass.__func__(cls)

    def with_flags(self, cells: dict[int, tuple[int, int]]) -> dict:
        d = surprise_fixture(SPAWNS)
        layer = d["layers"][0]
        tiles = list(layer.get("tiles", [0] * vp.CELLS))
        flags = [0] * vp.CELLS
        for i, (tile, f) in cells.items():
            tiles[i] = tile
            flags[i] = f
        layer["tiles"], layer["flags"] = tiles, flags
        return d

    def flag_errs(self, errs):
        return [e for e in errs if "flag" in e or "empty tile" in e]

    def test_bits_4_and_5_accepted(self):
        d = self.with_flags({1000: (1, 0x10), 1001: (1, 0x11), 1002: (0, 0x20), 1003: (0, 0x22)})
        errs, _ = self.run_docs({"flags": d})
        self.assertEqual(self.flag_errs(errs), [])

    def test_bit_6_is_reserved(self):
        d = self.with_flags({1000: (1, 0x40)})
        errs, _ = self.run_docs({"flags": d})
        self.assertIn("chunks/flags.json: layers[0] cell 1000 (x=8,y=31): reserved flag bits 6-7 set", errs)

    def test_empty_tile_takes_only_open_air(self):
        d = self.with_flags({1000: (0, 0x10)})
        errs, _ = self.run_docs({"flags": d})
        self.assertIn("chunks/flags.json: layers[0] cell 1000 (x=8,y=31): empty tile with elevation/flags set", errs)
