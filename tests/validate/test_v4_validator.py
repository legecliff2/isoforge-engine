#!/usr/bin/env python3
"""Validator tests for the @4 documents and item@1 (ctest: content_validate_v4).

Table-driven positive/negative cases over the fixture pack in tests/validate/fixtures/v4/pack
(valid monster@4, character@4 and item@1 documents; see v4_helpers.py). Each case edits one document
and names the substring the validator must print; the "check N" tags are the numbers of
docs/proposals/VERSION_BUMP_BATCH.md section 8.3. Frozen-corpus checks live in test_migrations_v4.py.
"""
from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import v4_helpers as h  # noqa: E402
import checks_v4  # noqa: E402

D = "monsters/red_dragon.json"
B = "characters/barbarian_1.json"
P = "characters/paladin_1.json"
R = "characters/rogue_1.json"
SAVE_EFFECT = {"kind": "save_effect", "id": "x", "name": "X", "save": {"ability": "dex", "dc": 15},
               "damage": [{"dice": "1d6", "type": "fire"}], "on_success": "half"}

# (name, document, sets, dels, expected substring). Always an error unless the name starts with "warn_".
CASES = [
    # --- check 1: attack ids
    ("c1_on_hit_bonus_attack_unknown", D, [(["actions", 2, "on_hit", 0, "attack_id"], "nope")], [], "(check 1)"),
    ("c1_reaction_attack_unknown", D, [(["reactions", 2, "effect", "attack_id"], "nope")], [], "(check 1)"),
    ("c1_event_bonus_attack_unknown", R, [(["features", 0, "effect", "effects", 1],
                                          {"kind": "bonus_attack", "attack_id": "nope"})], [], "(check 1)"),
    # --- check 2: pools
    ("c2_usage_pool_unknown", D, [(["actions", 5, "usage", "pool"], "nope")], [], "(check 2)"),
    ("c2_spend_pool_unknown", P, [(["features", 1, "effect", "effects", 0], {"kind": "spend", "pool": "nope"})], [], "(check 2)"),
    ("c2_heal_pool_unknown", P, [(["features", 0, "effect", "pool"], "nope")], [], "(check 2)"),
    ("c2_cost_exceeds_constant_max", D, [(["actions", 5, "usage", "cost"], 3)], [], "exceeds the pool's max 2 (check 2)"),
    ("c2_trait_usage_pool_unknown", D, [(["traits", 1, "effect", "usage", "pool"], "nope")], [], "(check 2)"),
    # --- check 3: content ids resolve
    ("c3_spellcasting_spell_unresolved", D, [(["spellcasting", "spells", 0], "test.v4.spell.ghost")], [], "(check 3)"),
    ("c3_innate_at_will_unresolved", D, [(["spellcasting", "innate", "at_will", 0], "test.v4.spell.ghost")], [], "(check 3)"),
    ("c3_innate_per_day_unresolved", D, [(["spellcasting", "innate", "per_day", "3", 0], "test.v4.spell.ghost")], [], "(check 3)"),
    ("c3_innate_recharge_unresolved", D, [(["spellcasting", "innate", "recharge", 0, "spell"], "test.v4.spell.ghost")], [], "(check 3)"),
    ("c3_cast_reaction_unresolved", D, [(["reactions", 3, "effect", "spell"], "test.v4.spell.ghost")], [], "(check 3)"),
    # --- check 4: save_effect ids
    ("c4_duplicate_save_effect_id", D, [(["bonus_actions", 0, "id"], "fire_breath")], [], "save_effect id 'fire_breath' is already used by /actions/4 (check 4)"),
    # --- check 5: multiattack
    ("c5_option_attack_unknown", D, [(["traits", 4, "effect", "options", 0, "attacks", 0], "nope")], [], "(check 5)"),
    ("c5_duplicate_option_id", D, [(["traits", 4, "effect", "options", 1, "id"], "full")], [], "option ids must be unique (check 5)"),
    ("c5_shorthand_attack_unknown", D, [(["traits", 4, "effect"], {"kind": "multiattack", "attacks": ["bite", "nope"]})], [], "(check 5)"),
    # --- check 6 / 11: item references
    ("c6_equipment_item_unresolved", B, [(["equipment", 1, "item"], "test.v4.item.ghost")], [], "(check 6/11)"),
    # --- check 8: derived trigger ids and resource ids
    ("c8_trigger_id_collides_with_rider", D, [(["traits", 0, "id"], "bite"), (["actions", 0, "on_hit", 0], {"kind": "extra_damage", "damage": [{"dice": "1d4", "type": "fire"}]})], [], "derived trigger id 'bite#0'"),
    ("c8_duplicate_event_trait_id", D, [(["traits", 2, "id"], "amphibious_rage")], [], "derived trigger id 'amphibious_rage#0'"),
    ("c8_duplicate_resource_id", D, [(["resources", 1, "id"], "legendary_resistance")], [], "duplicate resource id 'legendary_resistance' (check 8)"),
    # --- check 9: the roll_attack / subtract_roll reservation is lifted by R-G7 (sim #114); see RG7_CASES below
    ("c9_roll_attack_takes_only_subtract_roll", D, [(["reactions", 1, "window"], {"kind": "roll_attack"})], [], "the roll_attack window only takes subtract_roll, not 'retaliate'"),
    ("c9_subtract_roll_needs_roll_attack", D, [(["reactions", 1, "effect"], {"kind": "subtract_roll", "dice": "1d4"})], [], "subtract_roll needs the roll_attack window"),
    # --- check 10: save_effect schema rules (one negative each)
    ("c10_no_damage_or_apply", D, [], [["actions", 4, "damage"], ["actions", 4, "on_success"]], "is not valid under any of the given schemas [closest: 'damage' is a required property]"),
    ("c10_damage_without_on_success", D, [], [["actions", 4, "on_success"]], "'on_success' is a required property"),
    ("c10_on_success_without_damage", D, [(["actions", 3, "on_success"], "half")], [], "dependency"),
    ("c10_point_without_range", D, [], [["actions", 6, "range_ft"]], "'range_ft' is a required property"),
    ("c10_save_effect_with_to_hit", D, [(["actions", 4, "to_hit"], 5)], [], "should not be valid under"),
    ("c10_attack_with_save", D, [(["actions", 1, "save"], {"ability": "dex", "dc": 10})], [], "should not be valid under"),
    ("c10_save_effect_without_save", D, [], [["actions", 4, "save"]], "'save' is a required property"),
    ("c10_on_hit_on_spell_attack_kind", D, [(["actions", 7, "on_hit"], [{"kind": "heal_self", "amount": 1}])], [], "should not be valid under"),
    # --- check 12 (warning)
    ("warn_c12_unreferenced_pool", D, [(["resources", 2], {"id": "spare", "max": 1, "recharge": "never"})], [], "never referenced"),
    ("warn_equipment_inert_with_item_objects", B, [(["inert_mechanics"], ["equipment"])], [], "marked inert but contains item objects"),
    ("warn_c16_cost_above_level1_value", B, [(["features", 0, "effect", "usage", "cost"], 3)], [], "unusable at low levels (check 16.6)"),
    # --- check 13: range_ft iff not (self and area)
    ("c13_range_with_self_area", D, [(["actions", 4, "range_ft"], 30)], [], "(check 13)"),
    ("c13_range_with_explicit_self_area", D, [(["actions", 3, "range_ft"], 30)], [], "(check 13)"),
    # --- check 14: monster pools are integers
    ("c14_monster_object_max", D, [(["resources", 0, "max"], {"per_level": 1})], [], "(check 14)"),
    ("c14_monster_table_max", D, [(["resources", 0, "max"], {"table": {"by_level": [{"level": 1, "value": 3}]}})], [], "(check 14)"),
    # --- check 16: level tables
    ("c16_levels_not_ascending", B, [(["resources", 0, "max", "table", "by_level", 1, "level"], 1)], [], "(check 16.1)"),
    ("c16_unlimited_not_last", B, [(["resources", 0, "max", "table", "by_level", 2], {"level": 6, "unlimited": True})], [], "(check 16.2)"),
    ("c16_float_token", B, [(["resources", 0, "max", "table", "by_level", 0, "value"], 2.0)], [], "float value"),
    ("c16_start_with_table", B, [(["resources", 0, "start"], 1)], [], "(check 16.5)"),
    ("c16_cost_above_largest_value", B, [(["features", 0, "effect", "usage", "cost"], 7)], [], "(check 16.6)"),
    ("c16_unlimited_in_damage_table", B, [(["features", 0, "effect", "modifiers", 0, "bonus_table", "by_level", 1, "unlimited"], True)], [], "Additional properties are not allowed ('unlimited'"),
    ("c16_bonus_table_levels_not_ascending", B, [(["features", 0, "effect", "modifiers", 0, "bonus_table", "by_level", 1, "level"], 1)], [], "(check 16.1)"),
    ("c16_table_must_start_at_level_1", B, [(["resources", 0, "max", "table", "by_level", 0, "level"], 2)], [], "is not valid"),
    ("c16_bonus_table_on_monster", D, [(["traits", 1, "effect", "modifier"], {"stat": "damage", "applies_to": "self", "duration": {"kind": "rounds", "n": 1}, "bonus_table": {"by_level": [{"level": 1, "value": 1}]}})], [], "(check 16.4)"),
    ("c16_bonus_and_bonus_table", B, [(["features", 0, "effect", "modifiers", 0, "bonus"], 2)], [], "should not be valid under {'required': ['bonus', 'bonus_table']}"),
    # --- authored inert is rejected on disk (stamped legacy shapes validate against the schema, not on disk)
    ("inert_on_reaction_entry", D, [(["reactions", 0], {"id": "p", "name": "P", "kind": "special", "inert": True})], [], "/reactions/0: 'inert' is reserved"),
    ("inert_on_bonus_action_entry", D, [(["bonus_actions", 1], {"id": "k", "name": "K", "kind": "melee_weapon_attack", "to_hit": 1, "reach": 5, "damage": [{"dice": "1d4", "type": "fire"}], "inert": True})], [], "/bonus_actions/1: 'inert' is reserved"),
    ("inert_on_special_bonus_action_entry", D, [(["bonus_actions", 2, "inert"], True)], [], "/bonus_actions/2: 'inert' is reserved"),
    ("inert_on_effect", D, [(["traits", 3, "effect", "inert"], True)], [], "'inert' is reserved"),
    # --- schema negatives per def
    ("schema_grapple_rider_rejected", D, [(["actions", 0, "on_hit", 0, "kind"], "grapple")], [], "grapple"),
    ("schema_unknown_predicate", R, [(["features", 0, "effect", "when", 0, "kind"], "full_moon")], [], "/when/0/kind: 'full_moon' is not one of"),
    ("schema_unknown_window", D, [(["reactions", 0, "window", "kind"], "tuesday")], [], "is not valid under any of"),
    ("schema_save_condition_without_on_success", D, [(["actions", 0, "on_hit", 0, "damage"], [{"dice": "1d4", "type": "fire"}])], [], "dependency"),
    ("schema_predicate_extra_field_in_arm", R, [(["features", 0, "effect", "when", 1, "is"], "melee")], [], "'is' is not one of ['kind']"),
    ("schema_resources_unknown_recharge", D, [(["resources", 0, "recharge"], "weekly")], [], "is not one of"),
    ("schema_cost_outside_enum", B, [(["features", 0, "effect", "cost"], "free")], [], "/cost: 'free' is not one of ['action', 'bonus_action', 'reaction']"),
    ("schema_native_save_effect_trait_kind", D, [(["traits", 3, "effect"], {"kind": "save_effect", "pool": "x"})], [], "is an action / reaction kind, not a trait effect kind"),
    ("schema_native_reaction_trait_kind", D, [(["traits", 3, "effect"], {"kind": "reaction", "pool": "x"})], [], "is an action / reaction kind, not a trait effect kind"),
    ("schema_apply_modifier_needs_modifier", B, [], [["features", 0, "effect", "modifiers"]], "'modifier' is a required property"),
    ("schema_modifier_stat_arm_forbids_field", B, [(["features", 0, "effect", "modifiers", 1, "bonus"], 1)], [], "/modifiers/1: 'bonus' is not one of"),
    ("schema_unknown_top_level_key", D, [(["mystery"], 1)], [], "Unevaluated properties"),
    ("schema_forward_compat_unknown_value", D, [(["forward_compat"], ["flying"])], [], "is not one of"),
    ("schema_forward_compat_duplicates", D, [(["forward_compat"], ["elevation", "elevation"])], [], "non-unique"),
    ("schema_equipment_object_needs_item", B, [(["equipment", 1], {"equipped": True})], [], "is not valid under any of"),
    ("schema_unlimited_with_value", B, [(["resources", 0, "max", "table", "by_level", 5, "value"], 9)], [], "is not valid under any of the given schemas"),
    # --- item@1
    ("item_c15_long_range_below_normal", "items/dagger.json", [(["range"], [60, 20])], [], "(check 15)"),
    ("item_c15_reach_10_without_property", "items/glaive.json", [(["properties"], ["heavy", "two_handed"])], [], "(check 15)"),
    ("item_c15_reach_property_with_5", "items/glaive.json", [(["reach"], 5)], [], "(check 15)"),
    ("item_c15_reach_property_without_reach", "items/glaive.json", [], [["reach"]], "(check 15)"),
    ("item_c15_reach_15", "items/longsword.json", [(["reach"], 15)], [], "(check 15)"),
    ("item_c17_bonus_attack_rider", "items/flame_tongue.json", [(["on_hit", 1], {"kind": "bonus_attack", "attack_id": "x"})], [], "(check 17)"),
    ("item_two_hyphen_handed_rejected", "items/glaive.json", [(["properties"], ["heavy", "reach", "two-handed"])], [], "is not one of"),
    ("item_weapon_needs_damage", "items/longsword.json", [], [["damage"]], "'damage' is a required property"),
    ("item_versatile_needs_versatile_damage", "items/longsword.json", [], [["versatile_damage"]], "'versatile_damage' is a required property"),
    ("item_versatile_damage_without_property", "items/dagger.json", [(["versatile_damage"], [{"dice": "1d6", "type": "piercing"}])], [], "should not be valid under"),
    ("item_armor_field_on_weapon", "items/longsword.json", [(["base_ac"], 12)], [], "'base_ac' is not one of"),
    ("item_heavy_armor_dex_bonus", "items/chain_mail.json", [(["dex_bonus"], True)], [], "False was expected"),
    ("item_medium_needs_dex_cap", "items/half_plate.json", [], [["dex_cap"]], "'dex_cap' is a required property"),
    ("item_light_forbids_dex_cap", "items/leather_armor.json", [(["dex_cap"], 2)], [], "should not be valid under"),
    ("item_shield_forbids_stealth", "items/shield.json", [(["stealth_disadvantage"], True)], [], "'stealth_disadvantage' is not one of"),
    ("item_magic_bonus_range", "items/flame_tongue.json", [(["magic_bonus"], 4)], [], "maximum"),
    ("item_float_cost", "items/shield.json", [(["cost_cp"], 1.5)], [], "float value"),
    ("item_unknown_category", "items/shield.json", [(["category"], "ring")], [], "is not one of"),
    ("item_unknown_rider_kind", "items/flame_tongue.json", [(["on_hit", 0, "kind"], "grapple")], [], "grapple"),
    ("item_flat_damage_negative", "items/blowgun.json", [(["damage", 0, "flat"], -1)], [], "is not valid under any of"),
]
EXPECT_WARNING = lambda name: name.startswith("warn_")


class V4Validator(unittest.TestCase):
    base: dict

    @classmethod
    def setUpClass(cls):
        cls.base = h.pack_fixtures()

    def test_fixture_pack_is_clean(self):
        errs, warns = h.validate_docs(self.base)
        self.assertEqual(errs, [])
        self.assertEqual(warns, [])

    def test_cases(self):
        for name, rel, sets, dels, expect in CASES:
            with self.subTest(name):
                docs = dict(self.base)
                docs[rel] = h.edit(self.base[rel], sets, dels)
                errs, warns = h.validate_docs(docs)
                if EXPECT_WARNING(name):
                    self.assertEqual(errs, [], errs)
                    self.assertTrue(any(expect in w for w in warns), warns)
                else:
                    self.assertTrue(any(expect in e for e in errs), (expect, errs))

    def test_api_level_rule(self):
        # A pack containing any @4 or item@1 document must declare api_level >= 2 (section 8.5, validator error).
        errs, _ = h.validate_docs(self.base, api_level=1)
        self.assertTrue(any(e.startswith("manifest.json: api_level 1 is below 2") for e in errs), errs)
        only_item = {k: v for k, v in self.base.items() if k.startswith("items/")}
        self.assertTrue(any("item@1" in e for e in h.validate_docs(only_item, api_level=1)[0]))
        # level 2 is fine; a pack with no @4/item document may stay at level 1 (checked by the whole suite on content/packs).
        self.assertEqual(h.validate_docs(only_item, api_level=2)[0], [])

    def test_check_18_bonus_table_forbidden_in_spell_apply_modifier(self):
        spell = {"effects": [{"kind": "apply_modifier", "modifier": {"stat": "damage", "bonus_table": {"by_level": [{"level": 1, "value": 1}]}}}]}
        self.assertTrue(any("(check 18)" in e for e in checks_v4.check_spell_modifiers(spell)))
        spell["effects"][0] = {"kind": "apply_modifier", "modifiers": [{"stat": "ac", "bonus": 1}, {"stat": "damage", "bonus_table": {}}]}
        self.assertEqual(len(checks_v4.check_spell_modifiers(spell)), 1)
        ok = {"effects": [{"kind": "apply_modifier", "modifier": {"stat": "attack_roll", "die": "1d4"}}, {"kind": "heal"}]}
        self.assertEqual(checks_v4.check_spell_modifiers(ok), [])
        # row 12 landed (sim #96): spell@1 declares the additive apply_modifier kind; check 18 still holds on top of the schema.
        _reg, by_tag = h.registry()
        self.assertIn("apply_modifier", str(by_tag["spell@1"]["$defs"]["effect"]["properties"]["kind"]))

    def test_check_7_forward_compat_direct(self):
        doc = h.edit(self.base[D], [(["forward_compat"], ["elevation", "flying", "flying"])])
        ctx = {"ids": set(), "spell_ids": set(), "items": set(), "external_ok": lambda a: True}
        errs, _ = checks_v4.check_stat_block_v4("monster", doc, ctx)
        self.assertTrue(any("check 7" in e and "flying" in e for e in errs), errs)
        self.assertTrue(any("unique" in e and "check 7" in e for e in errs), errs)

    def test_srd51_escape_hatch_for_items_and_spells(self):
        # An srd51.* id that no pack defines resolves with the deprecated escape hatch only for packs that
        # do not depend on srd51.core (same rule as spells); this pack depends on it, so it must resolve.
        errs, _ = h.validate_docs(dict(self.base, **{B: h.edit(self.base[B], [(["equipment", 1, "item"], "srd51.item.longsword")])}))
        self.assertTrue(any("(check 6/11)" in e for e in errs), errs)

    def test_stamped_legacy_entries_fail_only_on_disk(self):
        entry = {"id": "p", "name": "P", "kind": "special", "inert": True}
        doc = h.edit(self.base[D], [(["reactions", 0], entry)])
        errs, _ = h.validate_docs(dict(self.base, **{D: doc}))
        self.assertEqual(len(errs), 1, errs)
        self.assertIn("/reactions/0: 'inert' is reserved", errs[0])
        e2, _ = h.validate_in_memory(doc, spell_ids={"srd51.spell.shield"})
        self.assertEqual([e for e in e2 if "reserved" in e], [])   # in-memory migration output: accepted


# ---- timed modifier properties (sim #96) and bonus-attack feature conventions (sim #100) ----------------------
EXTRA = h.FIXTURES / "extra"
MOD = ["features", 0, "effect", "modifiers", 0]   # timed_modifiers_1.json: mage_armor_like's ac floor modifier
MONK = "characters/monk_1.json"
TIMED = "characters/timed_1.json"
SPELL = "spells/guiding_bolt.json"


def extra(name: str) -> dict:
    import json
    return json.loads((EXTRA / name).read_text(encoding="utf-8"))


# (name, fixture file, rel path, sets, dels, expected substring); "warn_" cases must validate with that warning.
EXTRA_CASES = [
    ("mod_consume_bad_value", "timed_modifiers_1.json", TIMED, [(["features", 4, "effect", "modifier", "consume"], "forever")], [], "is not one of"),
    ("mod_consume_float", "timed_modifiers_1.json", TIMED, [(["features", 4, "effect", "modifier", "consume"], 1)], [], "is not one of"),
    ("mod_speed_sub_on_ac", "timed_modifiers_1.json", TIMED, [(MOD + ["speed_sub"], 5)], [], "'speed_sub' is not one of"),
    ("mod_speed_sub_zero", "timed_modifiers_1.json", TIMED, [(["features", 2, "effect", "modifier", "speed_sub"], 0)], [], "less than the minimum"),
    ("mod_speed_sub_float", "timed_modifiers_1.json", TIMED, [(["features", 2, "effect", "modifier", "speed_sub"], 10.0)], [], "float value"),
    ("mod_ac_floor_zero", "timed_modifiers_1.json", TIMED, [(MOD + ["ac_floor"], 0)], [], "less than the minimum"),
    ("mod_ac_floor_41", "timed_modifiers_1.json", TIMED, [(MOD + ["ac_floor"], 41)], [], "greater than the maximum"),
    ("mod_ac_floor_on_damage", "timed_modifiers_1.json", TIMED, [(["features", 5, "effect", "modifiers", 1, "ac_floor"], 12)], [], "'ac_floor' is not one of"),
    ("mod_ac_floor_dex_without_floor", "timed_modifiers_1.json", TIMED, [(MOD + ["bonus"], 1)], [["features", 0, "effect", "modifiers", 0, "ac_floor"]], "'ac_floor' is a dependency of 'ac_floor_dex'"),
    ("mod_ac_needs_bonus_or_floor", "timed_modifiers_1.json", TIMED, [], [["features", 0, "effect", "modifiers", 0, "ac_floor"], ["features", 0, "effect", "modifiers", 0, "ac_floor_dex"]], "is not valid under any of the given schemas"),
    ("mod_per_turn_on_ac", "timed_modifiers_1.json", TIMED, [(MOD + ["per_turn"], True)], [], "'per_turn' is not one of"),
    ("mod_per_turn_not_boolean", "timed_modifiers_1.json", TIMED, [(["features", 3, "effect", "modifier", "per_turn"], 1)], [], "is not of type 'boolean'"),
    ("mod_when_on_temp_hp", "timed_modifiers_1.json", TIMED, [(["features", 3, "effect", "modifier", "when"], [{"kind": "not_crit"}])], [], "'when' is not one of"),
    ("mod_when_on_speed", "timed_modifiers_1.json", TIMED, [(["features", 2, "effect", "modifier", "when"], [{"kind": "not_crit"}])], [], "'when' is not one of"),
    ("mod_when_unknown_predicate", "timed_modifiers_1.json", TIMED, [(["features", 5, "effect", "modifiers", 1, "when", 0, "kind"], "moon_is_full")], [], "is not one of"),
    ("mod_unknown_property", "timed_modifiers_1.json", TIMED, [(MOD + ["floor"], 12)], [], "'floor' is not one of"),
    ("spell_apply_modifier_needs_modifier", "guiding_bolt.json", SPELL, [(["effects", 1, "modifiers"], [])], [["effects", 1, "modifier"]], "'modifier' is a required property"),
    ("spell_apply_modifier_bad_consume", "guiding_bolt.json", SPELL, [(["effects", 1, "modifier", "consume"], "x")], [], "is not one of"),
    ("spell_apply_modifier_bonus_table", "guiding_bolt.json", SPELL, [(["effects", 1, "modifier"], {"stat": "damage", "bonus_table": {"by_level": [{"level": 1, "value": 1}]}, "duration": {"kind": "rounds", "n": 1}})], [], "(check 18)"),
    ("spell_apply_modifier_unknown_field", "guiding_bolt.json", SPELL, [(["effects", 1, "range"], 5)], [], "Additional properties"),
    # bonus-attack conventions (docs/CONTENT_CONVENTIONS.md section 2)
    ("bonus_martial_arts_needs_unarmed", "monk_1.json", MONK, [(["actions", 0, "properties"], ["monk"])], [], "feature 'martial_arts' needs a melee_weapon_attack action with the 'unarmed' property"),
    ("bonus_flurry_needs_unarmed_melee", "monk_1.json", MONK, [(["actions", 0, "kind"], "ranged_weapon_attack"), (["actions", 0, "range"], [20, 60]), (["actions", 0, "reach"], 5)], [["actions", 0, "reach"]], "feature 'flurry_of_blows' needs"),
    ("bonus_frenzy_needs_rage", "monk_1.json", MONK, [(["features", 3], {"id": "frenzy", "name": "Frenzy", "description": "x"})], [], "feature 'frenzy' needs a feature with id 'rage'"),
    ("warn_bonus_flurry_without_ki", "monk_1.json", MONK, [], [["resources", 0]], "declares no 'ki' pool"),
]


class TimedModifiersAndBonusAttacks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.base = h.pack_fixtures()

    def docs_with(self, rel: str, doc: dict) -> dict:
        d = dict(self.base)
        d[rel] = doc
        return d

    def test_positive_fixtures_validate_clean(self):
        for name, rel in (("monk_1.json", MONK), ("timed_modifiers_1.json", TIMED), ("true_strike.json", "spells/true_strike.json"),
                          ("guiding_bolt.json", SPELL)):
            with self.subTest(name):
                errs, warns = h.validate_docs(self.docs_with(rel, extra(name)))
                self.assertEqual(errs, [])
                self.assertEqual(warns, [])

    def test_cases(self):
        for name, fx, rel, sets, dels, expect in EXTRA_CASES:
            with self.subTest(name):
                errs, warns = h.validate_docs(self.docs_with(rel, h.edit(extra(fx), sets, dels)))
                if EXPECT_WARNING(name):
                    self.assertEqual(errs, [], errs)
                    self.assertTrue(any(expect in w for w in warns), warns)
                else:
                    self.assertTrue(any(expect in e for e in errs), (expect, errs))

    def test_frenzy_with_live_rage_is_clean_and_without_modifier_warns(self):
        barb = self.base[B]
        frenzy = {"id": "frenzy", "name": "Frenzy", "description": "x"}
        ok = h.edit(barb, [(["features", 1], frenzy)])
        errs, warns = h.validate_docs(self.docs_with(B, ok))
        self.assertEqual((errs, warns), ([], []))
        no_mod = h.edit(barb, [(["features", 1], frenzy), (["features", 0, "effect"], {"kind": "heal_pool", "pool": "rage"})])
        errs, warns = h.validate_docs(self.docs_with(B, no_mod))
        self.assertEqual(errs, [], errs)
        self.assertTrue(any("'<id>#rage#m0'" in w or "#rage#m0" in w for w in warns), warns)

    def test_two_weapon_fighting_and_light_weapons_need_nothing(self):
        # No requirement is invented: two light weapons with or without the style validate (monk_1 has the style; drop it).
        doc = h.edit(extra("monk_1.json"), [], [["features", 2]])
        errs, warns = h.validate_docs(self.docs_with(MONK, doc))
        self.assertEqual((errs, warns), ([], []))

    def test_flurry_pool_id_ki_counts_as_referenced(self):
        # check 12 must not call the ki pool declaration-only: the sim spends it by convention.
        errs, warns = h.validate_docs(self.docs_with(MONK, extra("monk_1.json")))
        self.assertFalse([w for w in warns if "(check 12)" in w], warns)


# ---- R-G7 (sim #114): roll_attack / subtract_roll reactions, reroll_ones / great_weapon_fighting / evasion ------------
BARD = "monsters/goblin_bard.json"
HALF = "characters/halfling_fighter_1.json"
CW = ["reactions", 0]   # goblin_bard.json: Cutting Words (roll_attack, subtract_roll 1d6, pool bardic_inspiration)

# (name, fixture file, rel path, sets, dels, expected substring); "warn_" cases must validate with that warning only.
RG7_CASES = [
    # reactions: the loader's errors
    ("cw_pool_unknown", "goblin_bard.json", BARD, [(CW + ["usage", "pool"], "nope")], [], "pool 'nope' is not a resources[].id of this document (check 2)"),
    ("cw_cost_zero", "goblin_bard.json", BARD, [(CW + ["usage", "cost"], 0)], [], "less than the minimum of 1"),
    ("cw_cost_negative", "goblin_bard.json", BARD, [(CW + ["usage", "cost"], -1)], [], "less than the minimum of 1"),
    ("cw_cost_float", "goblin_bard.json", BARD, [(CW + ["usage", "cost"], 1.0)], [], "float value"),
    ("cw_cost_string", "goblin_bard.json", BARD, [(CW + ["usage", "cost"], "1")], [], "is not of type 'integer'"),
    ("cw_cost_above_u16", "goblin_bard.json", BARD, [(CW + ["usage", "cost"], 65536), (["resources", 0, "max"], 70000)], [], "cost must be an integer 1..65535, got 65536"),
    ("cw_cost_above_pool_max", "goblin_bard.json", BARD, [(CW + ["usage", "cost"], 4)], [], "cost 4 exceeds the pool's max 3 (check 2)"),
    ("cw_needs_dice", "goblin_bard.json", BARD, [], [CW + ["effect", "dice"]], "'dice' is a required property"),
    ("cw_zero_dice", "goblin_bard.json", BARD, [(CW + ["effect", "dice"], "0d6")], [], "does not match"),
    ("cw_bad_dice", "goblin_bard.json", BARD, [(CW + ["effect", "dice"], "1d7")], [], "does not match"),
    ("cw_extra_effect_field", "goblin_bard.json", BARD, [(CW + ["effect", "bonus"], 1)], [], "'bonus' is not one of ['kind', 'dice']"),
    ("cw_pool_usage_extra_field", "goblin_bard.json", BARD, [(CW + ["usage", "uses"], 1)], [], "'uses' is not one of ['kind', 'pool', 'cost']"),
    ("cw_pool_usage_needs_pool", "goblin_bard.json", BARD, [], [CW + ["usage", "pool"]], "'pool' is a required property"),
    ("cw_window_takes_only_subtract_roll", "goblin_bard.json", BARD, [(["reactions", 1, "effect"], {"kind": "impose_disadvantage"})], [], "the roll_attack window only takes subtract_roll, not 'impose_disadvantage'"),
    ("cw_subtract_roll_in_damaged", "goblin_bard.json", BARD, [(["reactions", 1, "window", "kind"], "damaged")], [], "subtract_roll needs the roll_attack window"),
    ("cw_subtract_roll_in_ally_attacked", "goblin_bard.json", BARD, [(["reactions", 1, "window"], {"kind": "ally_attacked"})], [], "subtract_roll needs the roll_attack window"),
    ("cw_melee_and_ranged_only", "goblin_bard.json", BARD, [(["reactions", 1, "window", "ranged_only"], True)], [], "melee_only and ranged_only together"),
    # reactions: the loader lists these as unsupported and skips them (the file loads): warnings
    ("warn_cw_per_rest_usage", "goblin_bard.json", BARD, [(CW + ["usage"], {"kind": "per_rest", "uses": 1, "rest": "short"})], [], "compiles only a 'pool' usage"),
    ("warn_cw_usage_on_other_effect", "goblin_bard.json", BARD, [(CW + ["effect"], {"kind": "impose_disadvantage"})], [], "a usage is compiled only on a roll_attack subtract_roll reaction"),
    ("warn_cw_source_ally", "goblin_bard.json", BARD, [(["reactions", 1, "window", "source"], "ally_within_5ft")], [], "are not compiled"),
    ("warn_cw_not_visible", "goblin_bard.json", BARD, [(["reactions", 1, "window", "visible"], False)], [], "are not compiled"),
    ("warn_cw_subtract_roll_in_being_hit", "goblin_bard.json", BARD, [(["reactions", 1, "window"], {"kind": "being_hit"})], [], "subtract_roll in window 'being_hit' is not compiled"),
    # features / traits
    ("lucky_rolls_empty", "halfling_fighter_1.json", HALF, [(["features", 1, "effect", "rolls"], [])], [], "should be non-empty"),
    ("lucky_rolls_damage", "halfling_fighter_1.json", HALF, [(["features", 1, "effect", "rolls"], ["save", "damage"])], [], "'damage' is not one of ['attack', 'check', 'save']"),
    ("lucky_rolls_not_array", "halfling_fighter_1.json", HALF, [(["features", 1, "effect", "rolls"], "save")], [], "is not of type 'array'"),
    ("lucky_rolls_bad_on_monster", "goblin_bard.json", BARD, [(["traits", 1, "effect", "rolls"], ["initiative"])], [], "'initiative' is not one of"),
]


class RG7ReactionsAndFeatures(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.base = h.pack_fixtures()

    def docs_with(self, *pairs) -> dict:
        d = dict(self.base)
        for rel, doc in pairs:
            d[rel] = doc
        return d

    def test_positive_fixtures_validate_clean(self):
        # goblin_bard: Cutting Words (pool usage, default cost), an ungated melee_only one, an explicit cost 2; evasion and
        # reroll_ones traits. halfling_fighter_1: reroll_ones with and without rolls, great_weapon_fighting, evasion.
        errs, warns = h.validate_docs(self.docs_with((BARD, extra("goblin_bard.json")), (HALF, extra("halfling_fighter_1.json"))))
        self.assertEqual(errs, [])
        self.assertEqual(warns, [])

    def test_cases(self):
        for name, fx, rel, sets, dels, expect in RG7_CASES:
            with self.subTest(name):
                errs, warns = h.validate_docs(self.docs_with((rel, h.edit(extra(fx), sets, dels))))
                if EXPECT_WARNING(name):
                    self.assertEqual(errs, [], errs)
                    self.assertTrue(any(expect in w for w in warns), warns)
                else:
                    self.assertTrue(any(expect in e for e in errs), (expect, errs))

    def test_open_kinds_ignore_extra_fields(self):
        # common@1 effects stay open: the loader ignores fields it does not read, so the schema does too.
        doc = h.edit(extra("halfling_fighter_1.json"), [(["features", 2, "effect", "note"], "x"), (["features", 3, "effect", "x"], 1)])
        self.assertEqual(h.validate_docs(self.docs_with((HALF, doc))), ([], []))

    def test_pool_counts_as_referenced(self):
        # check 12: a reaction usage.pool references the pool (no declaration-only warning).
        errs, warns = h.validate_docs(self.docs_with((BARD, extra("goblin_bard.json"))))
        self.assertFalse([w for w in warns if "(check 12)" in w], warns)
        no_usage = h.edit(extra("goblin_bard.json"), [], [CW + ["usage"], ["reactions", 2, "usage"]])
        _errs, warns = h.validate_docs(self.docs_with((BARD, no_usage)))
        self.assertTrue([w for w in warns if "(check 12)" in w], warns)

    def test_kinds_are_live_at_3_and_known_but_inert_at_2(self):
        # sim #114 registers the three kinds with no minimum version: live at @3 (no unknown-kind warning, bad shapes are
        # schema errors through trait_active) and known at @1/@2, where effects are inert and findings stay warnings.
        for tag in ("character@3", "character@2"):
            with self.subTest(tag):
                doc = h.edit(extra("halfling_fighter_1.json"), [(["$schema"], tag)])
                for k in ("resources", "forward_compat", "inert_mechanics"):
                    doc.pop(k, None)
                ok_errs, ok_warns = h.validate_docs({HALF: doc})
                self.assertEqual(ok_errs, [])
                self.assertFalse([w for w in ok_warns if "unknown effect kind" in w], ok_warns)
                bad = h.edit(doc, [(["features", 1, "effect", "rolls"], ["damage"])])
                errs, warns = h.validate_docs({HALF: bad})
                if tag == "character@3":
                    self.assertTrue(any("'damage' is not one of" in e for e in errs), errs)
                else:
                    self.assertEqual(errs, [], errs)
                    self.assertTrue(any("'damage' is not one of" in w and "inert in character@2" in w for w in warns), warns)

    def test_reservation_is_lifted(self):
        self.assertFalse(hasattr(checks_v4, "RESERVED_WINDOWS"))
        self.assertFalse(hasattr(checks_v4, "RESERVED_REACTION_EFFECTS"))
        self.assertIn("roll_attack", checks_v4.COMPILED_WINDOWS)
        for tag in ("monster@3", "monster@4", "character@3", "character@4"):
            self.assertTrue(checks_v4.OPEN_EFFECT_KINDS_RG7 <= checks_v4.TAG_TABLE[tag].effect_kinds, tag)


if __name__ == "__main__":
    unittest.main()
