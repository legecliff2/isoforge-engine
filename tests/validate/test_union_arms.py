#!/usr/bin/env python3
"""allowed == required + optional, per arm (ctest: content_validate_arms).

docs/proposals/VERSION_BUMP_BATCH.md 3.1 "closed-union convention": a closed base object lists every
property of every arm; each arm is `if <discriminator> const ... then {required, propertyNames: {enum:
<exactly the arm's allowed names>}}`. A test asserts for each arm that `allowed == required U optional`
as documented. The documented sets below are written out by hand from the proposal (so a schema edit
that widens or narrows an arm without the doc changing fails here), for every propertyNames union:
common@2 usage, predicate, rider, modifier, reaction_effect, item@1, and spell@1 zone / zone_effect / delayed_expiry (plus the
R-G15 spell effect arms).
"""
from __future__ import annotations

import copy
import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCHEMAS = ROOT / "schemas"


def load(rel):
    return json.loads((SCHEMAS / rel).read_text(encoding="utf-8"))


S = frozenset
# {discriminator value(s): (required, optional)} -- names INCLUDE the discriminator and, for modifier, `duration`
# (required by the base object).
USAGE = {("at_will",): (S("kind".split()), S()),
         ("recharge",): (S({"kind", "min"}), S()),
         ("per_day",): (S({"kind", "uses"}), S()),
         ("per_rest",): (S({"kind", "uses", "rest"}), S()),
         ("pool",): (S({"kind", "pool"}), S({"cost"}))}
PREDICATE = {("weapon",): (S({"kind", "is"}), S()),
             ("advantage_or_ally_adjacent",): (S({"kind"}), S()),
             ("target_creature_type", "not_creature_type", "owner_creature_type"): (S({"kind", "creature_types"}), S()),
             ("damage_type_is", "damage_type_not"): (S({"kind", "types"}), S()),
             ("not_crit",): (S({"kind"}), S()),
             ("moved_at_least_ft",): (S({"kind", "ft"}), S()),
             ("self_hp_below",): (S({"kind", "num", "den"}), S()),
             ("save",): (S({"kind", "ability"}), S({"dc", "dc_from_damage", "base"})),
             ("target_surprised", "ally_near_target", "weapon_attack", "subject_is_caster", "target_below_max_hp"): (S({"kind"}), S()),
             ("damage_at_most",): (S({"kind", "amount"}), S()),
             ("target_tag", "not_target_tag"): (S({"kind", "tag"}), S())}
# when: rider-level predicates (sim #170), read only in on_hit[] of attack actions
RIDER = {("extra_damage",): (S({"kind", "damage"}), S({"scale", "when"})),
         ("save_condition",): (S({"kind", "save"}), S({"apply", "damage", "on_success", "when", "zero_hp"})),
         ("apply_condition",): (S({"kind", "apply"}), S({"when"})),
         ("heal_self", "temp_hp"): (S({"kind"}), S({"dice", "amount", "when"})),
         ("set_hp",): (S({"kind", "value"}), S({"when"})),
         ("bonus_attack",): (S({"kind", "attack_id"}), S({"when"})),
         ("area_save",): (S({"kind", "area", "save", "damage", "on_success"}), S({"when"})),
         ("spend",): (S({"kind"}), S({"pool", "cost", "slot", "when"})),
         ("grapple",): (S({"kind"}), S({"escape_dc", "when"}))}   # R-G8 hit rider (sim #104); on_hit[] of actions[] weapon attacks only
_M = {"applies_to", "stacks", "consume"}   # consume: any stat (section 6.2b, docs/CONTENT_CONVENTIONS.md 1)
MODIFIER = {("attack_roll",): (S({"stat", "duration"}), S({"bonus", "die", "mode", "when"} | _M)),
            ("save", "ability_check"): (S({"stat", "duration"}), S({"bonus", "die", "mode", "ability"} | _M)),
            ("ac",): (S({"stat", "duration"}), S({"bonus", "ac_floor", "ac_floor_dex", "when"} | _M)),   # bonus or ac_floor
            ("speed",): (S({"stat", "duration", "speed_scale"}), S({"speed_sub"} | _M)),
            ("damage",): (S({"stat", "duration"}), S({"bonus", "bonus_table", "die", "damage_type", "when"} | _M)),
            ("temp_hp",): (S({"stat", "duration", "bonus"}), S({"per_turn"} | _M)),
            ("resistance",): (S({"stat", "duration", "damage_type"}), S(_M)),
            ("attacks_against",): (S({"stat", "duration", "mode"}), S({"when"} | _M)),
            ("no_reactions", "no_healing"): (S({"stat", "duration"}), S(_M))}   # 7.2u markers
REACTION_EFFECT = {("ac_bonus",): (S({"kind", "bonus"}), S()),
                   ("reduce_damage",): (S({"kind"}), S({"fraction", "dice"})),
                   ("impose_disadvantage",): (S({"kind"}), S()),
                   ("retaliate",): (S({"kind", "damage"}), S({"save", "on_success"})),
                   ("attack",): (S({"kind", "attack_id"}), S()),
                   ("cast",): (S({"kind", "spell"}), S()),
                   ("subtract_roll",): (S({"kind", "dice"}), S())}
# spell@1 R-G11 zones (docs/CONTENT_CONVENTIONS.md section 4).
_ZOPT = {"flags", "move", "chooses_excluded", "needs_conscious_owner", "triggers",
         "aura_modifier", "aura_bonus_ability", "aura_immunity", "aura_allies_only"}   # + R-G11 PR 2 D7 aura keys (4.6)
ZONE = {("sphere", "cube"): (S({"shape", "size_ft", "anchor"}), S(_ZOPT | {"upcast_size_ft"})),
        ("cylinder",): (S({"shape", "size_ft", "height_ft", "anchor"}), S(_ZOPT | {"upcast_size_ft"})),
        ("line",): (S({"shape", "length_ft", "height_ft", "anchor"}), S(_ZOPT | {"ring_size_ft", "sided", "side_ft"}))}
ZONE_EFFECT = {("save",): (S({"kind", "ability"}), S({"damage", "on_success", "condition", "description"})),   # damage or condition
               ("damage",): (S({"kind", "damage"}), S({"condition", "description"})),
               ("apply_condition",): (S({"kind", "condition"}), S({"description"}))}
# spell@1 delayed_damage expiry (R-G15, sim #120; docs/CONTENT_CONVENTIONS.md section 6).
DELAYED_EXPIRY = {("rounds",): (S({"kind", "n"}), S()),
                  ("until_end_of_target_next_turn",): (S({"kind"}), S())}
# spell@1 $defs/effect arms of the R-G15 kinds. The effect union is the file's older if/then pattern (open base, each arm
# closed with additionalProperties: false); these arms also carry a propertyNames allow-list, checked in SpellEffectArmsR120.
SPELL_EFFECT_R120 = {"hp_pool": (S({"kind", "dice", "condition"}), S({"ends_on_damage", "excludes_creature_type", "description"})),
                     "dispel": (S({"kind"}), S({"check", "description"})),
                     "revive": (S({"kind"}), S({"within_rounds", "description"})),
                     "end_condition": (S({"kind", "conditions"}), S({"description"})),
                     "delayed_damage": (S({"kind", "damage", "expiry"}), S({"applies_to", "description"})),
                     "sustained_damage": (S({"kind", "damage"}), S({"description"}))}
# R-G9 spell@1 arms (sim #144, spell_loader.cpp forced_move / teleport): same closed if/then pattern.
SPELL_EFFECT_RG9 = {"forced_move": (S({"kind", "feet"}), S({"mode", "up_to", "max_size", "provokes_oa", "applies_to", "description"})),
                    "teleport": (S({"kind", "max_feet"}), S({"needs_sight", "description"}))}
# R-G12 transforms (VERSION_BUMP_BATCH 7.2q; docs/CONTENT_CONVENTIONS.md section 9): closed objects with a propertyNames
# allow-list. spell@1 polymorph is an arm of the file's if/then effect union; the common@2 ones are $defs of their own.
SPELL_EFFECT_RG12 = {"polymorph": (S({"kind", "forms"}), S({"max_cr", "save"}))}
COMMON2_RG12 = {"wild_shape_effect": (S({"kind", "pool", "forms", "max_cr_by_level"}),
                                      S({"cost", "class", "no_fly_below", "no_swim_below"})),
                "wild_shape_level_row": (S({"level", "max_cr"}), S()),
                "shapechanger_effect": (S({"kind", "forms"}), S({"cost"})),
                "shapechanger_form": (S({"id", "name"}), S({"size", "armor_class", "speed", "actions", "creature_type"}))}
_ICOMMON = {"source", "cost_cp", "weight_oz", "description"}
_IBASE = S({"$schema", "id", "name", "category"})
ITEM = {("weapon",): (_IBASE | {"weapon_class"}, S(_ICOMMON | {"reach", "range", "damage", "versatile_damage", "properties", "qualifiers", "magic_bonus", "on_hit"})),
        ("armor",): (_IBASE | {"armor_category", "base_ac", "dex_bonus"}, S(_ICOMMON | {"dex_cap", "str_min", "stealth_disadvantage", "magic_bonus"})),
        ("shield",): (_IBASE, S(_ICOMMON | {"ac_bonus", "magic_bonus"})),
        ("valuable",): (_IBASE, S(_ICOMMON))}   # gems (srd51.item.diamond): priced materials, docs/CONTENT_CONVENTIONS.md section 7

# (label, schema locator, discriminator, documented table)
def unions():
    c2 = load("common/v2.schema.json")["$defs"]
    return [("common@2 usage", c2["usage"], "kind", USAGE), ("common@2 predicate", c2["predicate"], "kind", PREDICATE),
            ("common@2 rider", c2["rider"], "kind", RIDER), ("common@2 modifier", c2["modifier"], "stat", MODIFIER),
            ("common@2 reaction_effect", c2["reaction_effect"], "kind", REACTION_EFFECT),
            ("item@1", load("item/v1.schema.json"), "category", ITEM),
            ("spell@1 zone", load("spell/v1.schema.json")["$defs"]["zone"], "shape", ZONE),
            ("spell@1 zone_effect", load("spell/v1.schema.json")["$defs"]["zone_effect"], "kind", ZONE_EFFECT),
            ("spell@1 delayed_expiry", load("spell/v1.schema.json")["$defs"]["delayed_expiry"], "kind", DELAYED_EXPIRY)]


def required_names(node):
    """Every name a sub-schema can require (required lists anywhere inside, dependentRequired values too)."""
    out = set()
    if isinstance(node, dict):
        for k, v in node.items():
            if k == "required" and isinstance(v, list):
                out |= set(v)
            elif k == "dependentRequired" and isinstance(v, dict):
                for a, b in v.items():
                    out |= {a} | set(b)
            elif k == "properties" and isinstance(v, dict):
                pass   # property definitions, not requirements
            else:
                out |= required_names(v)
    elif isinstance(node, list):
        for v in node:
            out |= required_names(v)
    return out


def arms(schema, disc):
    """{discriminator values tuple: (allowed names list, arm schema)} from the schema's allOf of if/then arms."""
    out = {}
    for sub in schema["allOf"]:
        if "if" not in sub or "propertyNames" not in sub.get("then", {}):
            continue
        cond = sub["if"]["properties"][disc]
        vals = tuple(cond["enum"]) if "enum" in cond else (cond["const"],)
        assert vals not in out, f"duplicate arm {vals}"
        out[vals] = (sub["then"]["propertyNames"]["enum"], sub["then"])
    return out


def check_union(label, schema, disc, doc) -> list[str]:
    problems = []
    found = arms(schema, disc)
    base_required = set(schema.get("required", []))
    base_props = set(schema["properties"])
    discs = schema["properties"][disc].get("enum")
    covered = [v for vals in found for v in vals]
    if sorted(covered) != sorted(set(covered)):
        problems.append(f"{label}: a {disc} value is covered by more than one arm")
    if discs is not None and sorted(covered) != sorted(discs):
        problems.append(f"{label}: {disc} enum {sorted(discs)} != arms {sorted(covered)}")
    if set(found) != set(doc):
        problems.append(f"{label}: arms {sorted(found)} != documented {sorted(doc)}")
    for vals, (names, then) in found.items():
        if vals not in doc:
            continue
        req, opt = doc[vals]
        tag = f"{label} arm {'/'.join(vals)}"
        if len(names) != len(set(names)):
            problems.append(f"{tag}: duplicate names in the allow-list")
        if set(names) != set(req) | set(opt):
            problems.append(f"{tag}: allowed {sorted(set(names))} != required U optional {sorted(set(req) | set(opt))}")
        arm_required = base_required | set(then.get("required", []))
        if not arm_required <= set(req):
            problems.append(f"{tag}: schema requires {sorted(arm_required - set(req))} which the doc does not")
        if not set(req) <= (arm_required | required_names(then)) | {disc}:
            # documented-required names must be required by the schema (directly or by oneOf/anyOf alternatives)
            gap = sorted(set(req) - (arm_required | required_names(then)) - {disc})
            if gap:
                problems.append(f"{tag}: documented required {gap} are never required by the schema")
        if not required_names(then) <= set(names) | base_required:
            problems.append(f"{tag}: requires {sorted(required_names(then) - set(names))}, which its own allow-list forbids")
        if not set(names) <= base_props:
            problems.append(f"{tag}: allow-list names {sorted(set(names) - base_props)} are not declared in the base properties")
    used = {n for names, _ in found.values() for n in names}
    if base_props - used:
        problems.append(f"{label}: base properties {sorted(base_props - used)} are allowed by no arm (orphans)")
    return problems


class SpellEffectArmsR120(unittest.TestCase):
    def test_r120_arms_are_closed_allow_lists(self):
        eff = load("spell/v1.schema.json")["$defs"]["effect"]
        found = {}
        for sub in eff["allOf"]:
            k = sub["if"]["properties"]["kind"]["const"]
            if k in SPELL_EFFECT_R120:
                found[k] = sub["then"]
        self.assertEqual(set(found), set(SPELL_EFFECT_R120))
        for k, then in found.items():
            with self.subTest(k):
                req, opt = SPELL_EFFECT_R120[k]
                self.assertIs(then["additionalProperties"], False)
                self.assertEqual(set(then["propertyNames"]["enum"]), set(req) | set(opt))
                self.assertEqual(set(then["properties"]), set(req) | set(opt))
                self.assertEqual(set(then["required"]), set(req))
                self.assertIn(k, eff["properties"]["kind"]["enum"])

    def test_rg9_arms_are_closed_allow_lists(self):
        eff = load("spell/v1.schema.json")["$defs"]["effect"]
        found = {sub["if"]["properties"]["kind"]["const"]: sub["then"] for sub in eff["allOf"]
                 if sub["if"]["properties"]["kind"]["const"] in SPELL_EFFECT_RG9}
        self.assertEqual(set(found), set(SPELL_EFFECT_RG9))
        for k, then in found.items():
            with self.subTest(k):
                req, opt = SPELL_EFFECT_RG9[k]
                self.assertIs(then["additionalProperties"], False)
                self.assertEqual(set(then["propertyNames"]["enum"]), set(req) | set(opt))
                self.assertEqual(set(then["properties"]), set(req) | set(opt))
                self.assertEqual(set(then["required"]), set(req))
                self.assertIn(k, eff["properties"]["kind"]["enum"])


class TransformArmsRG12(unittest.TestCase):
    def check_closed(self, node, req, opt):
        self.assertIs(node["additionalProperties"], False)
        self.assertEqual(set(node["propertyNames"]["enum"]), set(req) | set(opt))
        self.assertEqual(set(node["properties"]), set(req) | set(opt))
        self.assertEqual(set(node["required"]), set(req))

    def test_spell_polymorph_arm(self):
        eff = load("spell/v1.schema.json")["$defs"]["effect"]
        found = {sub["if"]["properties"]["kind"]["const"]: sub["then"] for sub in eff["allOf"]
                 if sub["if"]["properties"]["kind"]["const"] in SPELL_EFFECT_RG12}
        self.assertEqual(set(found), set(SPELL_EFFECT_RG12))
        for k, then in found.items():
            with self.subTest(k):
                self.check_closed(then, *SPELL_EFFECT_RG12[k])
                self.assertIn(k, eff["properties"]["kind"]["enum"])

    def test_common2_transform_defs(self):
        c2 = load("common/v2.schema.json")["$defs"]
        for name, (req, opt) in COMMON2_RG12.items():
            with self.subTest(name):
                self.check_closed(c2[name], req, opt)
        arms = {a["$ref"].rsplit("/", 1)[1] for a in c2["effect"]["anyOf"][2]["oneOf"] if "$ref" in a}   # gaze (7.2p) is inline
        self.assertLessEqual({"wild_shape_effect", "shapechanger_effect"}, arms)
        # closed: not reachable through the open effect_live arm
        self.assertLessEqual({"wild_shape", "shapechanger"}, set(c2["effect_live"]["allOf"][2]["properties"]["kind"]["not"]["enum"]))


class GrabContainArms(unittest.TestCase):
    """VERSION_BUMP_BATCH 7.2w: the closed transparent arm and the additive apply keys."""
    TRANSPARENT = (S({"kind", "dc"}), S())
    APPLY = (S({"condition"}), S({"duration", "repeat_save", "escape", "object", "max_size", "contain", "excludes_formless"}))
    CONTAIN = (S({"kind", "damage"}), S({"regurgitate"}))

    def test_transparent_arm(self):
        c2 = load("common/v2.schema.json")["$defs"]
        t = c2["transparent_effect"]
        self.assertFalse(t["additionalProperties"])
        self.assertEqual(set(t["required"]), set(self.TRANSPARENT[0]))
        self.assertEqual(set(t["properties"]), set(self.TRANSPARENT[0] | self.TRANSPARENT[1]))
        self.assertEqual((t["properties"]["dc"]["minimum"], t["properties"]["dc"]["maximum"]), (1, 30))
        arms = {a["$ref"].rsplit("/", 1)[1] for a in c2["effect"]["anyOf"][2]["oneOf"] if "$ref" in a}
        self.assertIn("transparent_effect", arms)
        nots = [a["properties"]["kind"]["not"] for a in c2["effect_live"]["allOf"] if "properties" in a]
        self.assertIn({"const": "transparent"}, nots)

    def test_apply_keys(self):
        a = load("common/v2.schema.json")["$defs"]["apply"]
        self.assertEqual(set(a["required"]), set(self.APPLY[0]))
        self.assertEqual(set(a["properties"]), set(self.APPLY[0] | self.APPLY[1]))
        c = a["properties"]["contain"]
        self.assertEqual(set(c["required"]), set(self.CONTAIN[0]))
        self.assertEqual(set(c["properties"]), set(self.CONTAIN[0] | self.CONTAIN[1]))
class ClassFeatureHooks72y(unittest.TestCase):
    """VERSION_BUMP_BATCH 7.2y: bonus_vs (closed), the save_effect control keys, apply_modifier scope, requires_sight."""
    BONUS_VS = (S({"creature_types", "dice"}), S())
    CONTROL = S({"control", "control_excludes_creature_types", "destroy_undead_max_cr8", "target_creature_types", "duration"})

    def test_bonus_vs_closed_on_both_damage_defs(self):
        c2 = load("common/v2.schema.json")["$defs"]
        b = c2["bonus_vs"]
        self.assertFalse(b["additionalProperties"])
        self.assertEqual(set(b["required"]), set(self.BONUS_VS[0]))
        self.assertEqual(set(b["properties"]), set(self.BONUS_VS[0] | self.BONUS_VS[1]))
        self.assertTrue(b["properties"]["dice"]["$ref"].endswith("/dice"))
        for k in ("damage", "rider_damage"):
            self.assertEqual(c2[k]["properties"]["bonus_vs"], {"$ref": "#/$defs/bonus_vs"})

    def test_control_keys_save_effect_only(self):
        a = load("common/v2.schema.json")["$defs"]["action"]
        self.assertLessEqual(set(self.CONTROL), set(a["properties"]))
        last = a["allOf"][-1]
        self.assertEqual(last["if"]["properties"]["kind"]["const"], "save_effect")
        self.assertEqual({r["required"][0] for r in last["else"]["not"]["anyOf"]}, set(self.CONTROL))
        self.assertIn({"required": ["control"]}, a["allOf"][3]["then"]["anyOf"])

    def test_scope_one_value_and_requires_sight(self):
        c2 = load("common/v2.schema.json")["$defs"]
        sc = c2["apply_modifier_effect"]["properties"]["scope"]
        self.assertFalse(sc["additionalProperties"])
        self.assertEqual(sc["minProperties"], 1)   # 7.2z: weapon / attack / ability, at least one
        self.assertEqual(sc["properties"]["weapon"], {"enum": ["wielded"]})
        self.assertEqual(c2["reaction"]["properties"]["window"]["properties"]["requires_sight"]["type"], "boolean")


class BarbarianHooks72z(unittest.TestCase):
    """VERSION_BUMP_BATCH 7.2z: scope keys, ends_early / blocks / advantage_on / on_end / passive / when, bonus_attack."""
    APPLY = S({"kind", "cost", "usage", "modifier", "modifiers", "scope", "ends_early", "blocks", "advantage_on", "on_end",
               "passive", "when"})

    def test_apply_modifier_keys(self):
        am = load("common/v2.schema.json")["$defs"]["apply_modifier_effect"]
        self.assertFalse(am["additionalProperties"])
        self.assertEqual(set(am["properties"]), set(self.APPLY))
        sc = am["properties"]["scope"]
        self.assertFalse(sc["additionalProperties"])
        self.assertEqual(sc["minProperties"], 1)
        self.assertEqual({k: v["enum"] for k, v in sc["properties"].items()},
                         {"weapon": ["wielded"], "attack": ["melee_weapon", "ranged_weapon"], "ability": ["str", "dex"]})
        self.assertEqual(am["properties"]["ends_early"]["items"]["enum"], ["no_attack_or_damage_since_last_turn", "unconscious"])
        self.assertEqual(am["properties"]["blocks"]["items"]["enum"], ["casting", "concentration"])
        for k in ("ends_early", "blocks"):
            self.assertTrue(am["properties"][k]["uniqueItems"])
        self.assertEqual(set(am["properties"]["advantage_on"]["properties"]), {"checks", "saves"})
        oe = am["properties"]["on_end"]
        self.assertEqual((oe["required"], oe["properties"]["exhaustion"]["minimum"], oe["properties"]["exhaustion"]["maximum"]),
                         (["exhaustion"], 1, 6))
        w = am["properties"]["when"]
        self.assertFalse(w["additionalProperties"])
        self.assertEqual(set(w["properties"]), {"save", "source_seen", "not_conditions"})

    def test_bonus_attack_arm(self):
        c2 = load("common/v2.schema.json")["$defs"]
        b = c2["bonus_attack_effect"]
        self.assertFalse(b["additionalProperties"])
        self.assertEqual(set(b["required"]), {"kind", "requires_effect"})
        self.assertEqual(set(b["properties"]), {"kind", "requires_effect"})
        arms = {a["$ref"].rsplit("/", 1)[1] for a in c2["effect"]["anyOf"][2]["oneOf"] if "$ref" in a}
        self.assertIn("bonus_attack_effect", arms)
        nots = [a["properties"]["kind"]["not"] for a in c2["effect_live"]["allOf"] if "properties" in a]
        self.assertIn({"const": "bonus_attack"}, nots)
        al = c2["ability_list"]
        self.assertTrue(al["uniqueItems"])
        self.assertEqual(al["items"]["enum"], ["str", "dex", "con", "int", "wis", "cha"])


class SpellMaterialObject(unittest.TestCase):
    def test_components_m_is_closed_with_the_documented_keys(self):
        # spell@1 components.m (priced materials, docs/CONTENT_CONVENTIONS.md section 7): a closed object, not a union.
        m = load("spell/v1.schema.json")["properties"]["components"]["properties"]["m"]
        self.assertIs(m["additionalProperties"], False)
        self.assertEqual(set(m["properties"]), {"text", "cost_cp", "consumed", "item"})
        self.assertEqual(m["required"], ["text"])
        self.assertEqual(m["properties"]["item"]["$ref"], "../common/v1.schema.json#/$defs/content_id")


class UnionArms(unittest.TestCase):
    def test_every_union_matches_the_documented_sets(self):
        for label, schema, disc, doc in unions():
            with self.subTest(label):
                self.assertEqual(check_union(label, schema, disc, doc), [])

    def test_unions_are_closed_bases(self):
        for label, schema, disc, doc in unions():
            with self.subTest(label):
                self.assertIs(schema.get("additionalProperties"), False)
                self.assertIn(disc, schema["required"] if disc != "stat" else ["stat"])

    def test_the_test_detects_drift(self):
        label, schema, disc, doc = [u for u in unions() if u[0] == "common@2 rider"][0]
        widened = copy.deepcopy(schema)
        for sub in widened["allOf"]:
            if sub["if"]["properties"]["kind"].get("const") == "set_hp":
                sub["then"]["propertyNames"]["enum"].append("scale")
        self.assertTrue(any("set_hp" in p and "!= required U optional" in p for p in check_union(label, widened, disc, doc)))
        narrowed = copy.deepcopy(schema)
        for sub in narrowed["allOf"]:
            if sub["if"]["properties"]["kind"].get("const") == "extra_damage":
                sub["then"]["propertyNames"]["enum"].remove("scale")
        self.assertTrue(check_union(label, narrowed, disc, doc))
        orphan = copy.deepcopy(schema)
        orphan["properties"]["zzz"] = {"type": "integer"}
        self.assertTrue(any("orphans" in p for p in check_union(label, orphan, disc, doc)))
        unreachable = copy.deepcopy(schema)
        for sub in unreachable["allOf"]:
            if sub["if"]["properties"]["kind"].get("const") == "bonus_attack":
                sub["then"]["propertyNames"]["enum"].remove("attack_id")
        self.assertTrue(any("forbids" in p for p in check_union(label, unreachable, disc, doc)))

    def test_a_field_added_to_a_base_is_forbidden_in_every_arm(self):
        """The allow-list in its strongest form: a new base property is rejected by every arm until one opts in."""
        from jsonschema import Draft202012Validator
        from referencing import Registry, Resource
        c2 = load("common/v2.schema.json")
        label, schema, disc, doc = [u for u in unions() if u[0] == "common@2 usage"][0]
        c2["$defs"]["usage"] = copy.deepcopy(schema)
        c2["$defs"]["usage"]["properties"]["zzz"] = {"type": "integer"}
        reg = Registry().with_resources([(load("common/v1.schema.json")["$id"], Resource.from_contents(load("common/v1.schema.json"))),
                                         (c2["$id"], Resource.from_contents(c2))])
        v = Draft202012Validator({"$ref": c2["$id"] + "#/$defs/usage"}, registry=reg)
        minimal = {"at_will": {}, "recharge": {"min": 5}, "per_day": {"uses": 1}, "per_rest": {"uses": 1, "rest": "short"},
                   "pool": {"pool": "p"}}
        for kind, extra in minimal.items():
            ok = {"kind": kind, **extra}
            self.assertTrue(v.is_valid(ok), kind)
            self.assertFalse(v.is_valid({**ok, "zzz": 1}), kind)


class ControlConditionsArms(unittest.TestCase):
    """VERSION_BUMP_BATCH 7.2v: additive save-effect keys (spell@1) and their common@2 defs."""
    SAVE = (S({"kind", "ability"}), S({"damage", "on_success", "condition", "repeat_save", "control",
                                         "control_excludes_creature_types", "control_min_int", "destroy_undead_max_cr8",
                                         "description", "applies_to"}))

    def test_save_arm_keys(self):
        eff = load("spell/v1.schema.json")["$defs"]["effect"]
        arm = [a["then"] for a in eff["allOf"] if a["if"]["properties"]["kind"].get("const") == "save"][0]
        req, opt = self.SAVE
        self.assertEqual(set(arm["required"]), set(req))
        self.assertEqual(set(arm["properties"]), set(req | opt))
        self.assertIn({"required": ["control"]}, arm["anyOf"])

    def test_common_defs(self):
        d = load("common/v2.schema.json")["$defs"]
        self.assertEqual(d["control_kind"]["enum"], ["turned", "fear", "laughter", "command"])
        self.assertEqual(d["control_repeat_save"]["enum"], ["end_of_turn_unseen", "end_of_turn_or_damage_advantage"])
        self.assertEqual(d["destroy_undead_max_cr8"]["enum"], [4, 8, 16, 24, 32])



if __name__ == "__main__":
    unittest.main()
