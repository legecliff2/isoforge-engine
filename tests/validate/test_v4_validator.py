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
    # --- check 9: reserved names
    ("c9_roll_attack_window", D, [(["reactions", 0, "window", "kind"], "roll_attack")], [], "(check 9, Q6)"),
    ("c9_subtract_roll_effect", D, [(["reactions", 0, "effect"], {"kind": "subtract_roll", "dice": "1d4"})], [], "(check 9, Q6)"),
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
        # spell@1 does not declare apply_modifier yet (row 12): today the schema rejects the kind outright.
        _reg, by_tag = h.registry()
        self.assertNotIn("apply_modifier", str(by_tag["spell@1"]["$defs"]["effect"]["properties"]["kind"]))

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


if __name__ == "__main__":
    unittest.main()
