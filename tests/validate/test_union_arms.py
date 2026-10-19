#!/usr/bin/env python3
"""allowed == required + optional, per arm (ctest: content_validate_arms).

docs/proposals/VERSION_BUMP_BATCH.md 3.1 "closed-union convention": a closed base object lists every
property of every arm; each arm is `if <discriminator> const ... then {required, propertyNames: {enum:
<exactly the arm's allowed names>}}`. A test asserts for each arm that `allowed == required U optional`
as documented. The documented sets below are written out by hand from the proposal (so a schema edit
that widens or narrows an arm without the doc changing fails here), for every propertyNames union:
common@2 usage, predicate, rider, modifier, reaction_effect, item@1, and spell@1 zone / zone_effect.
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
             ("target_creature_type", "not_creature_type"): (S({"kind", "creature_types"}), S()),
             ("damage_type_is", "damage_type_not"): (S({"kind", "types"}), S()),
             ("not_crit",): (S({"kind"}), S()),
             ("moved_at_least_ft",): (S({"kind", "ft"}), S()),
             ("self_hp_below",): (S({"kind", "num", "den"}), S()),
             ("save",): (S({"kind", "ability"}), S({"dc", "dc_from_damage", "base"}))}
RIDER = {("extra_damage",): (S({"kind", "damage"}), S({"scale"})),
         ("save_condition",): (S({"kind", "save"}), S({"apply", "damage", "on_success"})),
         ("apply_condition",): (S({"kind", "apply"}), S()),
         ("heal_self", "temp_hp"): (S({"kind"}), S({"dice", "amount"})),
         ("set_hp",): (S({"kind", "value"}), S()),
         ("bonus_attack",): (S({"kind", "attack_id"}), S()),
         ("area_save",): (S({"kind", "area", "save", "damage", "on_success"}), S()),
         ("spend",): (S({"kind"}), S({"pool", "cost", "slot"})),
         ("grapple",): (S({"kind"}), S({"escape_dc"}))}   # R-G8 hit rider (sim #104); on_hit[] of actions[] weapon attacks only
_M = {"applies_to", "stacks", "consume"}   # consume: any stat (section 6.2b, docs/CONTENT_CONVENTIONS.md 1)
MODIFIER = {("attack_roll",): (S({"stat", "duration"}), S({"bonus", "die", "mode", "when"} | _M)),
            ("save", "ability_check"): (S({"stat", "duration"}), S({"bonus", "die", "mode", "ability"} | _M)),
            ("ac",): (S({"stat", "duration"}), S({"bonus", "ac_floor", "ac_floor_dex", "when"} | _M)),   # bonus or ac_floor
            ("speed",): (S({"stat", "duration", "speed_scale"}), S({"speed_sub"} | _M)),
            ("damage",): (S({"stat", "duration"}), S({"bonus", "bonus_table", "die", "damage_type", "when"} | _M)),
            ("temp_hp",): (S({"stat", "duration", "bonus"}), S({"per_turn"} | _M)),
            ("resistance",): (S({"stat", "duration", "damage_type"}), S(_M)),
            ("attacks_against",): (S({"stat", "duration", "mode"}), S({"when"} | _M))}
REACTION_EFFECT = {("ac_bonus",): (S({"kind", "bonus"}), S()),
                   ("reduce_damage",): (S({"kind"}), S({"fraction", "dice"})),
                   ("impose_disadvantage",): (S({"kind"}), S()),
                   ("retaliate",): (S({"kind", "damage"}), S({"save", "on_success"})),
                   ("attack",): (S({"kind", "attack_id"}), S()),
                   ("cast",): (S({"kind", "spell"}), S()),
                   ("subtract_roll",): (S({"kind", "dice"}), S())}
# spell@1 R-G11 zones (docs/CONTENT_CONVENTIONS.md section 4).
_ZOPT = {"flags", "move", "chooses_excluded", "needs_conscious_owner", "triggers"}
ZONE = {("sphere", "cube"): (S({"shape", "size_ft", "anchor"}), S(_ZOPT)),
        ("cylinder",): (S({"shape", "size_ft", "height_ft", "anchor"}), S(_ZOPT))}
ZONE_EFFECT = {("save",): (S({"kind", "ability"}), S({"damage", "on_success", "condition", "description"})),   # damage or condition
               ("damage",): (S({"kind", "damage"}), S({"condition", "description"})),
               ("apply_condition",): (S({"kind", "condition"}), S({"description"}))}
_ICOMMON = {"source", "cost_cp", "weight_oz", "description"}
_IBASE = S({"$schema", "id", "name", "category"})
ITEM = {("weapon",): (_IBASE | {"weapon_class"}, S(_ICOMMON | {"reach", "range", "damage", "versatile_damage", "properties", "qualifiers", "magic_bonus", "on_hit"})),
        ("armor",): (_IBASE | {"armor_category", "base_ac", "dex_bonus"}, S(_ICOMMON | {"dex_cap", "str_min", "stealth_disadvantage", "magic_bonus"})),
        ("shield",): (_IBASE, S(_ICOMMON | {"ac_bonus", "magic_bonus"}))}

# (label, schema locator, discriminator, documented table)
def unions():
    c2 = load("common/v2.schema.json")["$defs"]
    return [("common@2 usage", c2["usage"], "kind", USAGE), ("common@2 predicate", c2["predicate"], "kind", PREDICATE),
            ("common@2 rider", c2["rider"], "kind", RIDER), ("common@2 modifier", c2["modifier"], "stat", MODIFIER),
            ("common@2 reaction_effect", c2["reaction_effect"], "kind", REACTION_EFFECT),
            ("item@1", load("item/v1.schema.json"), "category", ITEM),
            ("spell@1 zone", load("spell/v1.schema.json")["$defs"]["zone"], "shape", ZONE),
            ("spell@1 zone_effect", load("spell/v1.schema.json")["$defs"]["zone_effect"], "kind", ZONE_EFFECT)]


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


if __name__ == "__main__":
    unittest.main()
