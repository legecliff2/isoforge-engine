#!/usr/bin/env python3
"""Validator tests for the @4 documents and item@1 (ctest: content_validate_v4).

Table-driven positive/negative cases over the fixture pack in tests/validate/fixtures/v4/pack
(valid monster@4, character@4 and item@1 documents; see v4_helpers.py). Each case edits one document
and names the substring the validator must print; the "check N" tags are the numbers of
docs/proposals/VERSION_BUMP_BATCH.md section 8.3. Frozen-corpus checks live in test_migrations_v4.py.
"""
from __future__ import annotations

import json
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
    # --- R-G5 static kinds (sim #107): known at @4, shapes the sim would refuse to load are errors
    ("rg5_attack_bonus_bad_attacks", R, [(["features", 1], {"id": "archery", "name": "A", "effect": {"kind": "attack_bonus", "attacks": "thrown", "amount": 2}})], [], "attack_bonus.attacks must be 'melee' or 'ranged' (R-G5)"),
    ("rg5_attack_bonus_float_amount", R, [(["features", 1], {"id": "archery", "name": "A", "effect": {"kind": "attack_bonus", "attacks": "ranged", "amount": 2.0}})], [], "attack_bonus.amount must be an integer"),
    ("rg5_crit_range_min_1", R, [(["features", 1], {"id": "crit", "name": "C", "effect": {"kind": "crit_range", "min": 1}})], [], "crit_range.min must be an integer in 2..20 (R-G5)"),
    ("rg5_crit_range_missing_min", R, [(["features", 1], {"id": "crit", "name": "C", "effect": {"kind": "crit_range"}})], [], "crit_range.min must be an integer in 2..20 (R-G5)"),
    ("rg5_ac_bonus_out_of_range", R, [(["features", 1], {"id": "defense", "name": "D", "effect": {"kind": "ac_bonus", "amount": 11}})], [], "ac_bonus.amount must be an integer in -10..10 (R-G5)"),
    ("rg5_ac_bonus_requires_armor_not_bool", R, [(["features", 1], {"id": "defense", "name": "D", "effect": {"kind": "ac_bonus", "amount": 1, "requires_armor": 1}})], [], "ac_bonus.requires_armor must be a boolean (R-G5)"),
    # --- authored inert is rejected on disk (stamped legacy shapes validate against the schema, not on disk)
    ("inert_on_reaction_entry", D, [(["reactions", 0], {"id": "p", "name": "P", "kind": "special", "inert": True})], [], "/reactions/0: 'inert' is reserved"),
    ("inert_on_bonus_action_entry", D, [(["bonus_actions", 1], {"id": "k", "name": "K", "kind": "melee_weapon_attack", "to_hit": 1, "reach": 5, "damage": [{"dice": "1d4", "type": "fire"}], "inert": True})], [], "/bonus_actions/1: 'inert' is reserved"),
    ("inert_on_special_bonus_action_entry", D, [(["bonus_actions", 2, "inert"], True)], [], "/bonus_actions/2: 'inert' is reserved"),
    ("inert_on_effect", D, [(["traits", 3, "effect", "inert"], True)], [], "'inert' is reserved"),
    # --- schema negatives per def
    ("schema_unknown_rider_kind", D, [(["actions", 0, "on_hit", 0, "kind"], "swallow")], [], "'swallow' is not one of"),
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
    ("item_gem_is_not_a_category", "items/shield.json", [(["category"], "gem")], [["ac_bonus"]], "/category: 'gem' is not one of ['weapon', 'armor', 'shield', 'valuable']"),
    ("item_valuable_forbids_ac_bonus", "items/shield.json", [(["category"], "valuable")], [], "/: 'ac_bonus' is not one of ['$schema', 'id', 'name', 'source', 'category', 'cost_cp', 'weight_oz', 'description']"),
    ("item_valuable_forbids_damage", "items/shield.json", [(["category"], "valuable"), (["damage"], [{"dice": "1d4", "type": "fire"}])], [["ac_bonus"]], "/: 'damage' is not one of"),
    ("item_valuable_forbids_value_gp", "items/shield.json", [(["category"], "valuable"), (["value_gp"], 300)], [["ac_bonus"]], "Additional properties are not allowed ('value_gp'"),
    ("item_valuable_float_cost", "items/shield.json", [(["category"], "valuable"), (["cost_cp"], 1.5)], [["ac_bonus"]], "float value"),
    ("item_unknown_rider_kind", "items/flame_tongue.json", [(["on_hit", 0, "kind"], "swallow")], [], "'swallow' is not one of"),
    ("item_c17_grapple_rider", "items/flame_tongue.json", [(["on_hit", 1], {"kind": "grapple", "escape_dc": 12})], [], "grapple riders are rejected in items"),
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

    def test_valuable_item(self):
        """item@1 category valuable (gems; docs/CONTENT_CONVENTIONS.md section 7): only the common item fields; an equipment
        entry naming it resolves (check 6/11) with or without value_gp."""
        gem = {"$schema": "item@1", "id": "test.v4.item.diamond", "name": "Diamond", "category": "valuable",
               "source": {"ref": "SRD 5.1", "license": "CC-BY-4.0"}, "description": "x"}
        docs = dict(self.base)
        docs["items/diamond.json"] = gem
        docs[B] = h.edit(self.base[B], [(["equipment", 5], {"item": "test.v4.item.diamond"}),
                                         (["equipment", 6], {"item": "test.v4.item.diamond", "value_gp": 300})])
        self.assertEqual(h.validate_docs(docs), ([], []))
        docs["items/diamond.json"] = {**gem, "cost_cp": 30000, "weight_oz": 0}
        self.assertEqual(h.validate_docs(docs), ([], []))
        # without the item doc: the value_gp entry stays valid (priced materials need no item@1), the bare one does not
        del docs["items/diamond.json"]
        errs, _ = h.validate_docs(docs)
        self.assertEqual([e for e in errs if "(check 6/11)" in e],
                         [f"{B}: /equipment/5: item 'test.v4.item.diamond' is not an item@1 in this pack or its depends (check 6/11)"])

    def test_rg5_static_kinds_are_known_at_4(self):
        """attack_bonus / crit_range / ac_bonus have sim hooks (#107): no unknown-kind warning, no error."""
        feats = [{"id": "archery", "name": "Archery", "effect": {"kind": "attack_bonus", "attacks": "ranged", "amount": 2}},
                 {"id": "crit", "name": "Improved Critical", "effect": {"kind": "crit_range", "min": 19}},
                 {"id": "defense", "name": "Defense", "effect": {"kind": "ac_bonus", "amount": 1, "requires_armor": True}}]
        docs = dict(self.base)
        docs[R] = h.edit(self.base[R], [(["features", 1 + i], f) for i, f in enumerate(feats)])
        errs, warns = h.validate_docs(docs)
        self.assertEqual(errs, [])
        self.assertEqual(warns, [])
        for k in ("attack_bonus", "crit_range", "ac_bonus"):
            self.assertIn(k, checks_v4.TAG_TABLE["character@4"].effect_kinds)
            self.assertIn(k, checks_v4.TAG_TABLE["monster@4"].effect_kinds)
            self.assertNotIn(k, checks_v4.TAG_TABLE["character@3"].effect_kinds)   # R-G5 kinds are known at @4 only

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
        # do not depend on srd51.core (same rule as spells); this pack depends on it, so it must resolve there.
        # srd51.core's item@1 ids are content ids of the set (C++ PackSet), so srd51.item.longsword resolves...
        errs, _ = h.validate_docs(dict(self.base, **{B: h.edit(self.base[B], [(["equipment", 1, "item"], "srd51.item.longsword")])}))
        self.assertFalse(any("(check 6/11)" in e for e in errs), errs)
        # ...and an srd51 id srd51.core does not define is an error.
        errs, _ = h.validate_docs(dict(self.base, **{B: h.edit(self.base[B], [(["equipment", 1, "item"], "srd51.item.vorpal_sword")])}))
        self.assertTrue(any("(check 6/11)" in e for e in errs), errs)

    def test_priced_material_value_gp(self):
        # Director ruling (priced material components): {"item": id, "value_gp": N} satisfies a spell's components.m when the
        # id matches and value_gp * 100 >= cost_cp; it needs no item@1 document (item@1 has no gem/valuable category yet).
        n = len(self.base[B]["equipment"])
        diamond = {"item": "srd51.item.diamond", "value_gp": 300}
        self.assertEqual(h.validate_docs(dict(self.base, **{B: h.edit(self.base[B], [(["equipment", n], diamond)])})), ([], []))
        zero = {"item": "test.v4.item.ghost_gem", "value_gp": 0}
        self.assertEqual(h.validate_docs(dict(self.base, **{B: h.edit(self.base[B], [(["equipment", n], zero)])}))[0], [])
        # value_gp on an entry that does resolve (a priced longsword) is fine too
        self.assertEqual(h.validate_docs(dict(self.base, **{B: h.edit(self.base[B], [(["equipment", 1, "value_gp"], 15)])}))[0], [])
        for bad, want in ((-1, "is not valid under any of"), (1.5, "float value 1.5 not allowed"), ("300", "is not valid under any of")):
            with self.subTest(bad):
                errs, _ = h.validate_docs(dict(self.base, **{B: h.edit(self.base[B], [(["equipment", n], dict(diamond, value_gp=bad))])}))
                self.assertTrue(any(want in e for e in errs), (want, errs))
        # without value_gp an unknown item still has to resolve (check 6/11); srd51.item.diamond is now a real
        # srd51.core item@1 doc (#132) indexed by the pack set (#134), so use an id no pack defines
        errs, _ = h.validate_docs(dict(self.base, **{B: h.edit(self.base[B], [(["equipment", n], {"item": "srd51.item.black_sapphire"})])}))
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
    ("cw_extra_effect_field", "goblin_bard.json", BARD, [(CW + ["effect", "bonus"], 1)], [], "'bonus' is not one of ['kind', 'dice', 'applies_to']"),
    ("cw_applies_to_bad_kind", "goblin_bard.json", BARD, [(CW + ["effect", "applies_to"], ["attack", "initiative"])], [], "'initiative' is not one of ['attack', 'save', 'check', 'damage']"),   # 7.2za
    ("cw_applies_to_repeated", "goblin_bard.json", BARD, [(CW + ["effect", "applies_to"], ["check", "check"])], [], "has non-unique elements"),   # 7.2za
    ("cw_applies_to_empty", "goblin_bard.json", BARD, [(CW + ["effect", "applies_to"], [])], [], "should be non-empty"),   # 7.2za
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


# ---- R-G8 grapple hit rider (sim #104, docs/CONTENT_CONVENTIONS.md section 5) --------------------------------
CRAB = "monsters/giant_crab_grappler.json"
GR = ["actions", 0, "on_hit", 0]   # giant_crab_grappler.json: claw grapple escape_dc 11

# (name, rel path, sets, dels, expected substring); "warn_" cases must validate with that warning only.
GRAPPLE_CASES = [
    # the loader's errors (sim: "bad grapple escape_dc")
    ("grapple_escape_dc_above_30", CRAB, [(GR + ["escape_dc"], 31)], [], "greater than the maximum of 30"),
    ("grapple_escape_dc_negative", CRAB, [(GR + ["escape_dc"], -1)], [], "less than the minimum of 0"),
    ("grapple_escape_dc_float", CRAB, [(GR + ["escape_dc"], 11.0)], [], "grapple escape_dc must be an integer 0..30, got 11.0"),
    ("grapple_escape_dc_string", CRAB, [(GR + ["escape_dc"], "11")], [], "is not of type 'integer'"),
    ("grapple_escape_dc_bool", CRAB, [(GR + ["escape_dc"], True)], [], "is not of type 'integer'"),
    ("grapple_extra_field", CRAB, [(GR + ["condition"], "restrained")], [], "'condition' is not one of ['kind', 'escape_dc', 'when']"),
    ("grapple_escape_dc_on_other_rider", CRAB, [(["actions", 1, "on_hit", 1, "escape_dc"], 12)], [], "'escape_dc' is not one of ['kind', 'damage', 'scale', 'when']"),
    ("grapple_in_on_event_effects", CRAB, [(["traits"], [{"id": "hold", "name": "Hold", "description": "x",
                                                          "effect": {"kind": "on_event", "on": "hit", "effects": [{"kind": "grapple", "escape_dc": 11}]}}])],
     [], "unknown rider kind 'grapple'"),
    ("grapple_on_save_effect", CRAB, [(["actions", 0], {"kind": "save_effect", "id": "x", "name": "X", "save": {"ability": "str", "dc": 11},
                                                        "apply": [{"condition": "grappled"}], "on_hit": [{"kind": "grapple"}]})], [], "should not be valid under"),
    # the file loads, the rider is ignored or overwritten: warnings
    # an untimed save_condition rider (no duration, no repeat_save): sim load error unless the condition ends by its own
    # rules (checks_v4.UNTIMED_ON_HIT_CONDITIONS = {prone}, sim #133 kEndsByOwnRules)
    ("save_condition_untimed_restrained", CRAB, [(["actions", 1, "on_hit", 1], {"kind": "save_condition", "save": {"ability": "str", "dc": 11},
                                                                               "apply": [{"condition": "restrained"}]})], [],
     "save_condition uses an unsupported duration"),
    ("save_condition_untimed_prone_with_untimed_restrained", CRAB, [(["actions", 1, "on_hit", 1], {"kind": "save_condition", "save": {"ability": "str", "dc": 11},
                                                                                                  "apply": [{"condition": "prone"}, {"condition": "restrained"}]})], [],
     "save_condition uses an unsupported duration (apply condition restrained"),
    ("save_condition_untimed_poisoned_in_on_event", CRAB, [(["traits"], [{"id": "trip", "name": "Trip", "description": "x",
                                                                         "effect": {"kind": "on_event", "on": "hit", "effects": [
                                                                             {"kind": "save_condition", "save": {"ability": "str", "dc": 11},
                                                                              "apply": [{"condition": "poisoned"}]}]}}])], [],
     "save_condition uses an unsupported duration"),
    ("warn_grapple_twice_on_one_attack", CRAB, [(["actions", 0, "on_hit", 1], {"kind": "grapple", "escape_dc": 13})], [], "the sim keeps only the last one"),
    ("warn_grapple_on_bonus_action", CRAB, [(["bonus_actions"], [{"id": "nip", "name": "Nip", "kind": "melee_weapon_attack", "to_hit": 3, "reach": 5,
                                                                   "targets": 1, "damage": [{"dice": "1d4", "type": "piercing"}],
                                                                   "on_hit": [{"kind": "grapple"}]}])], [], "this grapple rider never applies"),
]


class RG8GrappleRider(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.base = h.pack_fixtures()

    def docs_with(self, doc: dict) -> dict:
        d = dict(self.base)
        d[CRAB] = doc
        return d

    def test_positive_fixture_validates_clean(self):
        # claw: grapple escape_dc 11; pincer: grapple with no escape_dc (Athletics contest) followed by an extra_damage rider.
        self.assertEqual(h.validate_docs(self.docs_with(extra("giant_crab_grappler.json"))), ([], []))

    def test_untimed_prone_rider_is_accepted(self):
        # sim #133: an untimed prone save_condition rider loads (untracked), on_hit[] and on_event effects[] alike, alone or
        # after a timed condition; the validator reports nothing for it.
        rider = {"kind": "save_condition", "save": {"ability": "str", "dc": 11}, "apply": [{"condition": "prone"}]}
        mixed = {"kind": "save_condition", "save": {"ability": "str", "dc": 11},
                 "apply": [{"condition": "poisoned", "duration": {"kind": "rounds", "n": 1}}, {"condition": "prone"}]}
        on_event = [{"id": "trip", "name": "Trip", "description": "x",
                     "effect": {"kind": "on_event", "on": "hit", "effects": [rider]}}]
        for name, sets in (("on_hit", [(["actions", 1, "on_hit", 1], rider)]), ("on_hit_mixed", [(["actions", 1, "on_hit", 1], mixed)]),
                           ("on_event", [(["traits"], on_event)])):
            with self.subTest(case=name):
                doc = h.edit(extra("giant_crab_grappler.json"), sets)
                self.assertEqual(h.validate_docs(self.docs_with(doc)), ([], []))

    def test_escape_dc_bounds_are_accepted(self):
        for dc in (0, 1, 30):
            with self.subTest(dc):
                doc = h.edit(extra("giant_crab_grappler.json"), [(GR + ["escape_dc"], dc)])
                self.assertEqual(h.validate_docs(self.docs_with(doc)), ([], []))

    def test_cases(self):
        for name, rel, sets, dels, expect in GRAPPLE_CASES:
            with self.subTest(name):
                errs, warns = h.validate_docs(self.docs_with(h.edit(extra("giant_crab_grappler.json"), sets, dels)))
                if EXPECT_WARNING(name):
                    self.assertEqual(errs, [], errs)
                    self.assertTrue(any(expect in w for w in warns), warns)
                else:
                    self.assertTrue(any(expect in e for e in errs), (expect, errs))

    def test_grapple_rider_makes_no_trigger(self):
        # check 8: the loader compiles no trigger for a grapple rider, so 'claw#0' is free for a trait named 'claw'.
        trait = {"id": "claw", "name": "Claw Snap", "description": "x",
                 "effect": {"kind": "on_event", "on": "hit", "effects": [{"kind": "extra_damage", "damage": [{"dice": "1d4", "type": "bludgeoning"}]}]}}
        doc = h.edit(extra("giant_crab_grappler.json"), [(["traits"], [trait])])
        self.assertEqual(h.validate_docs(self.docs_with(doc)), ([], []))
        clash = h.edit(doc, [(GR, {"kind": "extra_damage", "damage": [{"dice": "1d4", "type": "fire"}]})])
        errs, _warns = h.validate_docs(self.docs_with(clash))
        self.assertTrue(any("derived trigger id 'claw#0'" in e for e in errs), errs)

    def test_grapple_is_a_rider_kind_only_in_common2(self):
        import json
        c2 = json.loads((Path(__file__).resolve().parents[2] / "schemas/common/v2.schema.json").read_text(encoding="utf-8"))
        self.assertIn("grapple", c2["$defs"]["rider"]["properties"]["kind"]["enum"])
        dc = c2["$defs"]["rider"]["properties"]["escape_dc"]
        self.assertEqual((dc["type"], dc["minimum"], dc["maximum"]), ("integer", 0, checks_v4.GRAPPLE_ESCAPE_DC_MAX))


# ---- R-G14 metamagic / sculpt_spells (sim #143, features.cpp apply_metamagic / apply_sculpt_spells) ----------------
MM = {"kind": "metamagic", "options": ["quickened", "twinned"], "pool": "rage"}   # barbarian_1: rage is a limited pool at level 1


def _feat(fid, eff):
    return {"id": fid, "name": fid, "description": "x", "effect": eff}


SORC = [(["features", 1], _feat("metamagic", MM)), (["features", 2], _feat("sculpt", {"kind": "sculpt_spells"}))]
SP = ["resources", 1]
SPARE = {"id": "spare", "max": 3, "recharge": "long_rest"}

# (name, sets, dels, expected substring) over barbarian_1 + SORC; "warn_" cases must validate with that warning only.
RG14_CASES = [
    ("mm_pool_unknown", [(["features", 1, "effect", "pool"], "nope")], [], "metamagic: metamagic pool 'nope' is not a limited resources[] pool"),
    ("mm_pool_unlimited", [(["resources", 0, "max", "table", "by_level"], [{"level": 1, "unlimited": True}])], [],
     "metamagic: metamagic pool 'rage' is not a limited resources[] pool"),
    ("mm_pool_not_string", [(["features", 1, "effect", "pool"], 3)], [], "is not of type 'string'"),
    ("mm_needs_pool", [], [["features", 1, "effect", "pool"]], "'pool' is a required property"),
    ("mm_needs_options", [], [["features", 1, "effect", "options"]], "'options' is a required property"),
    ("mm_empty_options", [(["features", 1, "effect", "options"], [])], [], "should be non-empty"),
    ("mm_unknown_option", [(["features", 1, "effect", "options"], ["loud"])], [], "'loud' is not one of"),
    ("mm_options_not_array", [(["features", 1, "effect", "options"], "quickened")], [], "is not of type 'array'"),
    ("mm_different_pools", [(SP, SPARE), (["features", 3], _feat("mm2", {"kind": "metamagic", "options": ["subtle"], "pool": "spare"}))], [],
     "mm2: metamagic declarations name different pools"),
    ("warn_mm_repeated_option", [(["features", 1, "effect", "options"], ["quickened", "quickened"])], [], "listed more than once"),
    ("warn_sculpt_extra_field", [(["features", 2, "effect", "radius"], 5)], [], "sculpt_spells takes no fields"),
    ("warn_sculpt_duplicate", [(["features", 3], _feat("sculpt2", {"kind": "sculpt_spells"}))], [], "duplicate 'sculpt_spells' effect"),
]


class RG14MetamagicSculpt(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.base = h.pack_fixtures()

    def sorc(self, sets=(), dels=()):
        return h.edit(h.edit(self.base[B], SORC, []), list(sets), list(dels))

    def run_doc(self, doc):
        d = dict(self.base)
        d[B] = doc
        return h.validate_docs(d)

    def test_positive_validates_clean(self):
        # two declarations on one pool OR their options; the pool counts as referenced (no check-12 warning)
        doc = self.sorc([(["features", 3], _feat("mm2", {"kind": "metamagic", "options": ["distant"], "pool": "rage"}))])
        self.assertEqual(self.run_doc(doc), ([], []))

    def test_cases(self):
        for name, sets, dels, expect in RG14_CASES:
            with self.subTest(name):
                errs, warns = self.run_doc(self.sorc(sets, dels))
                if EXPECT_WARNING(name):
                    self.assertEqual(errs, [], errs)
                    self.assertTrue(any(expect in w for w in warns), (expect, warns))
                else:
                    self.assertTrue(any(expect in e for e in errs), (expect, errs))

    def test_options_mirror_the_loader(self):
        arm = next(a["then"] for a in json.loads((h.ROOT / "schemas/common/v1.schema.json").read_text(encoding="utf-8"))["$defs"]["effect"]["allOf"]
                   if a["if"]["properties"]["kind"].get("const") == "metamagic")
        self.assertEqual(tuple(arm["properties"]["options"]["items"]["enum"]), checks_v4.METAMAGIC_OPTIONS)
        self.assertIn("sculpt_spells", checks_v4.NO_STACK_EFFECT_KINDS)
        self.assertNotIn("metamagic", checks_v4.NO_STACK_EFFECT_KINDS)   # several declarations add options


# ---- R-G12 transforms (VERSION_BUMP_BATCH 7.2q, Rules #156): polymorph / wild_shape / shapechanger / requires_humanoid_form --
WOLF, BEAR, DIRE, SNAKE = "srd51.monster.wolf", "srd51.monster.black_bear", "srd51.monster.dire_wolf", "srd51.monster.giant_poisonous_snake"
WS_POOL = {"id": "wild_shape", "max": 2, "recharge": "short_rest"}
WS = {"kind": "wild_shape", "pool": "wild_shape", "forms": [WOLF, BEAR, SNAKE],
      "max_cr_by_level": [{"level": 2, "max_cr": "1/4"}, {"level": 4, "max_cr": "1/2"}, {"level": 8, "max_cr": "1"}]}
DRUID = [(["classes", 0, "class"], "druid"), (["resources", 1], WS_POOL), (["features", 1], _feat("wild_shape", WS))]
SC = {"kind": "shapechanger", "forms": [{"id": "wolf", "name": "Wolf form", "size": "medium", "actions": ["bite"]},
                                        {"id": "bat", "name": "Bat form", "speed": {"walk": 5, "fly": 30}}]}
SHAPER = [(["shapechanger"], True), (["traits", 6], _feat("shapechanger", SC))]
WSF = ["features", 1, "effect"]
SCF = ["traits", 6, "effect"]
# (name, base, sets, dels, expected substring); "warn_" = valid with that warning
RG12_CASES = [
    # wild_shape (character feature)
    ("ws_unknown_pool", B, [(WSF + ["pool"], "nope")], [], "wild_shape: unknown pool 'nope'"),
    ("ws_pool_long_rest", B, [(["resources", 1, "recharge"], "long_rest")], [], "wild_shape: pool 'wild_shape' must recharge on short_rest"),
    ("ws_levels_not_ascending", B, [(WSF + ["max_cr_by_level", 1, "level"], 2)], [], "wild_shape: max_cr_by_level levels must be strictly ascending"),
    ("ws_levels_descending", B, [(WSF + ["max_cr_by_level", 1, "level"], 1)], [], "wild_shape: max_cr_by_level levels must be strictly ascending"),
    ("ws_unknown_form", B, [(WSF + ["forms", 3], "srd51.monster.unicorn")], [], "wild_shape: unknown form 'srd51.monster.unicorn'"),
    ("ws_not_a_beast", B, [(WSF + ["forms", 3], "srd51.monster.ogre")], [], "wild_shape: form 'srd51.monster.ogre' is not a beast"),
    ("ws_duplicate_form", B, [(WSF + ["forms", 3], WOLF)], [], f"wild_shape: duplicate form '{WOLF}'"),
    ("ws_two_declarations", B, [(["features", 2], _feat("ws2", WS))], [], "wild_shape: one declaration per document"),
    ("ws_needs_pool", B, [], [WSF + ["pool"]], "'pool' is a required property"),
    ("ws_needs_forms", B, [], [WSF + ["forms"]], "'forms' is a required property"),
    ("ws_needs_rows", B, [], [WSF + ["max_cr_by_level"]], "'max_cr_by_level' is a required property"),
    ("ws_empty_forms", B, [(WSF + ["forms"], [])], [], "should be non-empty"),
    ("ws_no_duration_key", B, [(WSF + ["duration_hours"], "half_level")], [], "duration_hours"),
    ("ws_level_21", B, [(WSF + ["max_cr_by_level", 2, "level"], 21)], [], "21 is greater than the maximum of 20"),
    ("ws_bad_cr", B, [(WSF + ["max_cr_by_level", 0, "max_cr"], "1/3")], [], "'1/3'"),
    ("ws_no_fly_below_0", B, [(WSF + ["no_fly_below"], 0)], [], "0 is less than the minimum of 1"),
    ("ws_humanoid_flag_on_itself", B, [(["features", 1, "requires_humanoid_form"], True)], [], "requires_humanoid_form on the wild_shape feature itself"),
    ("warn_ws_pool_max", B, [(["resources", 1, "max"], 3)], [], "wild_shape: pool 'wild_shape' max should be 2"),
    ("warn_ws_above_every_row", B, [(WSF + ["forms", 3], "srd51.monster.axe_beak"), (WSF + ["max_cr_by_level"], [{"level": 2, "max_cr": "1/8"}]),
                                    (WSF + ["forms"], [WOLF])], [], f"wild_shape: form '{WOLF}' above every max_cr_by_level row"),
    ("warn_ws_class_missing", B, [(["classes", 0, "class"], "barbarian")], [], "wild_shape: class 'druid' not in classes[]"),
    ("warn_ws_class_named_missing", B, [(WSF + ["class"], "ranger")], [], "wild_shape: class 'ranger' not in classes[]"),
    # shapechanger (monster trait)
    ("sc_needs_fact", D, [], [["shapechanger"]], "shapechanger trait requires shapechanger: true"),
    ("sc_fact_false", D, [(["shapechanger"], False)], [], "shapechanger trait requires shapechanger: true"),
    ("sc_duplicate_form_id", D, [(SCF + ["forms", 1, "id"], "wolf")], [], "shapechanger: duplicate form id 'wolf'"),
    ("sc_unknown_action", D, [(SCF + ["forms", 0, "actions"], ["bite", "howl"])], [], "shapechanger: form 'wolf' names unknown action 'howl'"),
    ("sc_two_declarations", D, [(["traits", 7], _feat("sc2", SC))], [], "shapechanger: one declaration per document"),
    ("sc_duplicate_action", D, [(SCF + ["forms", 0, "actions"], ["bite", "bite"])], [], "has non-unique elements"),
    ("sc_empty_forms", D, [(SCF + ["forms"], [])], [], "should be non-empty"),
    ("sc_form_extra_key", D, [(SCF + ["forms", 0, "hit_points"], {"average": 1})], [], "hit_points"),
    ("sc_form_needs_name", D, [], [SCF + ["forms", 0, "name"]], "'name' is a required property"),
    ("warn_sc_changes_nothing", D, [(SCF + ["forms", 1], {"id": "same", "name": "Same"})], [], "shapechanger: form 'same' changes nothing"),
    ("warn_sc_no_actions", D, [(SCF + ["forms", 0, "actions"], [])], [], "shapechanger: form 'wolf' allows no actions"),
    # EC #176: the loader's form key list (id, name, size, armor_class, speed, actions, creature_type); speed_walk is refused
    ("sc_form_speed_walk", D, [(SCF + ["forms", 0, "speed_walk"], 30)], [], "speed_walk"),
    ("sc_effect_extra_key", D, [(SCF + ["uses"], 1)], [], "uses"),
    # requires_humanoid_form
    ("warn_rhf_without_wild_shape", D, [(["traits", 0, "requires_humanoid_form"], True)], [], "requires_humanoid_form has no effect without wild_shape"),
    ("rhf_not_boolean", B, [(["features", 0, "requires_humanoid_form"], 1)], [], "is not of type 'boolean'"),
]


def _poly_spell(eff_sets=(), sets=(), dels=()):
    d = json.loads((h.CORE / "spells" / "hold_person.json").read_text(encoding="utf-8"))
    d.update(id="test.v4.spell.polymorph", name="Polymorph", level=4, school="transmutation")
    d["targets"] = {"count": 1}
    d["duration"] = {"kind": "hours", "amount": 1, "concentration": True}
    d["effects"] = [{"kind": "polymorph", "forms": [WOLF, BEAR]}]
    del d["scaling"]
    return h.edit(h.edit(d, [(["effects", 0] + p, v) for p, v in eff_sets]), list(sets), list(dels))


SPELL = "spells/polymorph.json"
EF = ["effects", 0]
# (name, sets, dels, expected): spell@1 polymorph
POLY_CASES = [
    ("pm_secondary", [(["effects"], [{"kind": "save", "ability": "wis", "condition": "paralyzed"}, {"kind": "polymorph", "forms": [WOLF]}])], [], "polymorph: primary effect only"),
    ("pm_two_targets", [(["targets", "count"], 2)], [], "polymorph: one target"),
    ("pm_area", [(["area"], {"shape": "sphere", "size_ft": 20})], [], "polymorph: one target"),
    ("pm_no_concentration", [(["duration"], {"kind": "hours", "amount": 1})], [], "polymorph: needs concentration"),
    ("pm_unknown_form", [(EF + ["forms", 2], "srd51.monster.t_rex")], [], "polymorph: unknown form 'srd51.monster.t_rex'"),
    ("pm_not_a_beast", [(EF + ["forms", 2], "srd51.monster.ogre")], [], "polymorph: form 'srd51.monster.ogre' is not a beast"),
    ("pm_duplicate", [(EF + ["forms", 2], WOLF)], [], f"polymorph: duplicate form '{WOLF}'"),
    ("pm_needs_forms", [], [EF + ["forms"]], "'forms' is a required property"),
    ("pm_bad_max_cr", [(EF + ["max_cr"], "level")], [], "max_cr"),
    ("pm_bad_save", [(EF + ["save"], {"ability": "wis", "dc": 12})], [], "save"),
    ("pm_extra_key", [(EF + ["duration"], 1)], [], "duration"),
    ("pm_too_many_forms", [(EF + ["forms"], [f"srd51.monster.m{i}" for i in range(256)])], [], "is too long"),
    ("warn_pm_above_max_cr", [(EF + ["max_cr"], "1/4")], [], f"polymorph: form '{BEAR}' above max_cr"),
]


class RG12Transforms(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.base = h.pack_fixtures()

    def docs(self, base, sets=(), dels=()):
        d = dict(self.base)
        first = DRUID if base == B else SHAPER
        d[base] = h.edit(h.edit(self.base[base], first, []), list(sets), list(dels))
        return d

    def test_druid_fixture_is_schema_valid(self):
        # extra/druid_4.json is the one wild_shape document the sim's end-to-end test (engine/core/sim/tests/
        # transforms_content_tests.cpp, "schema fixture") also loads, so the validator and the loader read the same keys.
        # wild_shape is a loaded kind since EC #176: no warning at all.
        d = dict(self.base)
        d["characters/druid_4.json"] = extra("druid_4.json")
        errs, warns = h.validate_docs(d)
        self.assertEqual(errs, [])
        self.assertEqual(warns, [])

    def check(self, name, errs, warns, expect):
        if name.startswith("warn_"):
            self.assertEqual(errs, [], errs)
            self.assertTrue(any(expect in w for w in warns), (expect, warns))
        else:
            self.assertTrue(any(expect in e for e in errs), (expect, errs))

    def test_cases(self):
        for name, base, sets, dels, expect in RG12_CASES:
            with self.subTest(name):
                self.check(name, *h.validate_docs(self.docs(base, sets, dels)), expect)

    def test_positive_no_unknown_kind_warnings(self):
        # forms resolve in the dependency pack (srd51.core); the uses pool counts as referenced (no check-12 warning)
        errs, warns = h.validate_docs(self.docs(B, [(["features", 0, "requires_humanoid_form"], True)]))
        self.assertEqual(errs, [])
        self.assertEqual([w for w in warns if "barbarian_1" in w], [])   # EC #176: wild_shape is loaded, no unknown-kind warning
        errs, warns = h.validate_docs(self.docs(D))
        self.assertEqual(errs, [])
        dw = [w for w in warns if "red_dragon" in w]
        self.assertFalse(any("unknown effect kind" in w for w in dw), dw)   # EC #176: shapechanger is loaded
        # the trait reads the #141 boolean: its reserved warning is dropped
        self.assertFalse(any("shapechanger is reserved" in w for w in dw), dw)

    def test_boolean_without_trait_stays_legal_and_reserved(self):
        d = dict(self.base)
        d[D] = h.edit(self.base[D], [(["shapechanger"], True)])
        errs, warns = h.validate_docs(d)
        self.assertEqual(errs, [])
        self.assertTrue(any("/shapechanger: shapechanger is reserved" in w for w in warns))

    def test_local_form_resolves(self):
        wolf = json.loads((h.CORE / "monsters" / "wolf.json").read_text(encoding="utf-8"))
        wolf["id"] = "test.v4.monster.wolf"
        d = self.docs(B, [(WSF + ["forms"], ["test.v4.monster.wolf"])])
        d["monsters/wolf.json"] = wolf
        errs, _ = h.validate_docs(d)
        self.assertEqual(errs, [])

    def test_polymorph_cases(self):
        for name, sets, dels, expect in POLY_CASES:
            with self.subTest(name):
                d = dict(self.base)
                d[SPELL] = _poly_spell((), sets, dels)
                self.check(name, *h.validate_docs(d), expect)

    def test_polymorph_positive(self):
        d = dict(self.base)
        d[SPELL] = _poly_spell([(["max_cr"], "1/2"), (["save"], {"ability": "wis"})])
        errs, warns = h.validate_docs(d)
        self.assertEqual(errs, [])
        self.assertEqual([w for w in warns if "polymorph" in w], [])   # Rules #171: no reserved warning
        d[SPELL] = _poly_spell([(["max_cr"], "target_level")])
        self.assertEqual(h.validate_docs(d)[0], [])

    def test_cr8(self):
        self.assertEqual([checks_v4.cr8(c) for c in ("0", "1/8", "1/4", "1/2", "1", "30")], [0, 1, 2, 4, 8, 240])
        self.assertIsNone(checks_v4.cr8("1/3"))




# ---- extra_attack (EC #179, sim features.cpp apply_extra_attack) -----------------------------------------------------
XA = ["features", 1]   # appended after the fixture's rage feature
# (name, base, sets, dels, expected substring); "warn_" = valid with that warning
XA_CASES = [
    ("xa_missing", B, [(XA, _feat("extra_attack", {"kind": "extra_attack"}))], [], "test.v4.character.barbarian_1: extra_attack: attacks_per_action must be an integer 2..4"),
    ("xa_one", B, [(XA, _feat("extra_attack", {"kind": "extra_attack", "attacks_per_action": 1}))], [], "extra_attack: attacks_per_action must be an integer 2..4"),
    ("xa_five", B, [(XA, _feat("extra_attack", {"kind": "extra_attack", "attacks_per_action": 5}))], [], "extra_attack: attacks_per_action must be an integer 2..4"),
    ("xa_string", B, [(XA, _feat("extra_attack", {"kind": "extra_attack", "attacks_per_action": "2"}))], [], "extra_attack: attacks_per_action must be an integer 2..4"),
    ("xa_bool", B, [(XA, _feat("extra_attack", {"kind": "extra_attack", "attacks_per_action": True}))], [], "extra_attack: attacks_per_action must be an integer 2..4"),
    ("xa_float", B, [(XA, _feat("extra_attack", {"kind": "extra_attack", "attacks_per_action": 2.5}))], [], "extra_attack: attacks_per_action must be an integer 2..4"),
    ("xa_unknown_key", B, [(XA, _feat("extra_attack", {"kind": "extra_attack", "attacks_per_action": 2, "count": 2}))], [], "extra_attack: unknown key 'count'"),
    ("xa_monster", D, [(["traits", 6], _feat("extra_attack", {"kind": "extra_attack", "attacks_per_action": 2}))], [], "extra_attack: only valid on characters"),
    ("warn_xa_twice", B, [(XA, _feat("extra_attack", {"kind": "extra_attack", "attacks_per_action": 2})),
                          (["features", 2], _feat("xa3", {"kind": "extra_attack", "attacks_per_action": 3}))], [],
     "extra_attack declared more than once"),
]


class ExtraAttack(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.base = h.pack_fixtures()

    def docs(self, base, sets=(), dels=()):
        d = dict(self.base)
        d[base] = h.edit(self.base[base], list(sets), list(dels))
        return d

    def test_valid_2_3_4(self):
        for n in (2, 3, 4):
            with self.subTest(n=n):
                errs, warns = h.validate_docs(self.docs(B, [(XA, _feat("extra_attack", {"kind": "extra_attack", "attacks_per_action": n}))]))
                self.assertEqual(errs, [])
                self.assertFalse(any("extra_attack" in w for w in warns), warns)

    def test_cases(self):
        for name, base, sets, dels, expect in XA_CASES:
            with self.subTest(name):
                errs, warns = h.validate_docs(self.docs(base, sets, dels))
                if name.startswith("warn_"):
                    self.assertEqual(errs, [], errs)
                    self.assertTrue(any(expect in w for w in warns), (expect, warns))
                else:
                    self.assertTrue(any(expect in e for e in errs), (expect, errs))

    def test_inert_below_4(self):
        # character@3: inert in the sim (hook's @4 gate; the frozen 3 -> 4 list cannot carry it): unknown-kind warning + hint
        self.assertIn("extra_attack", checks_v4.TAG_TABLE["character@4"].effect_kinds)
        self.assertNotIn("extra_attack", checks_v4.TAG_TABLE["character@3"].effect_kinds)
        self.assertNotIn("extra_attack", checks_v4.NEW_EFFECT_KINDS_AT_4)   # the frozen migration list is untouched

    def test_kinds_are_loaded_at_4(self):
        for k in ("wild_shape", "shapechanger", "extra_attack"):
            for typ in ("monster", "character"):
                with self.subTest(kind=k, typ=typ):
                    self.assertIn(k, checks_v4.TAG_TABLE[f"{typ}@4"].effect_kinds)

    def test_arm_mirrors_the_loader(self):
        d = json.loads((h.ROOT / "schemas/common/v2.schema.json").read_text(encoding="utf-8"))["$defs"]
        arm = d["extra_attack_effect"]
        self.assertEqual(set(arm["properties"]), set(checks_v4.EXTRA_ATTACK_KEYS))
        self.assertEqual((arm["properties"]["attacks_per_action"]["minimum"], arm["properties"]["attacks_per_action"]["maximum"]), (2, 4))
        self.assertEqual(set(d["shapechanger_form"]["properties"]),
                         {"id", "name", "size", "armor_class", "speed", "actions", "creature_type"})   # EC #176 only_keys
        self.assertEqual(set(d["shapechanger_effect"]["properties"]), {"kind", "cost", "forms"})


if __name__ == "__main__":
    unittest.main()


class BardHooks72za(unittest.TestCase):
    """7.2za: the Bardic Inspiration / Jack of All Trades shapes from the hand-off pass check_bard_hooks."""
    GD = {"kind": "grant_die", "die": "1d6", "class": "bard", "scale": [{"level": 5, "die": "1d8"}, {"level": 10, "die": "1d10"},
          {"level": 15, "die": "1d12"}], "range_ft": 60, "duration": {"kind": "rounds", "n": 100},
          "applies_to": ["attack", "save", "check"], "cost": "bonus_action", "usage": {"kind": "pool", "pool": "bardic_inspiration"}}
    JACK = {"kind": "half_proficiency", "on": ["checks", "initiative"]}

    def doc(self, schema="character@4"):
        return {"$schema": schema, "id": "t.bard", "features": [{"id": "bi", "effect": self.GD}, {"id": "jack", "effect": self.JACK}]}

    def test_valid_shapes(self):
        self.assertEqual(checks_v4.check_bard_hooks(self.doc()), [])

    def test_monster_and_order(self):
        self.assertTrue(any("not valid on monsters" in e for e in checks_v4.check_bard_hooks(self.doc("monster@4"))))
        d = self.doc()
        d["features"][0]["effect"] = dict(self.GD, scale=[{"level": 5, "die": "1d8"}, {"level": 5, "die": "1d10"}])
        self.assertTrue(any("must ascend" in e for e in checks_v4.check_bard_hooks(d)))
