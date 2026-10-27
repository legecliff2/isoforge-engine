#!/usr/bin/env python3
"""item@2 / monster@5 / character@5 schema and validator tests (ctest: content_validate_equip_v5).

docs/proposals/EQUIPMENT.md (#245) 7.1 (every new key optional) and 7.2 (item@1 -> @2, monster@4 -> @5, character@4 -> @5 are
identity migrations, #246 in C++): an @4 / item@1 document re-stamped to the new version validates unchanged and the old
versions still validate as before. Structural validator checks: 3.1 hands, 5.5 reasons, 5.2 PB table (5.6 strings), 6.2 light.
The derived-vs-printed assertion of 5.6 needs rules5e::derive_monster (#247) and is not tested here.
"""
from __future__ import annotations

import copy
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import v4_helpers as h  # noqa: E402
import validate_packs as vp  # noqa: E402

ROOT = h.ROOT
CORE = ROOT / "content" / "packs" / "srd51.core"
RESTAMP = {"monster@4": "monster@5", "character@4": "character@5", "item@1": "item@2"}
SRC = {"ref": "SRD 5.1", "license": "CC-BY-4.0"}
FLAT_FX = ROOT / "tests" / "validate" / "fixtures" / "monster_flat"


def core(rel: str) -> dict:
    return json.loads((CORE / rel).read_text(encoding="utf-8"))


def lantern() -> dict:
    return {"$schema": "item@2", "id": "test.v4.lantern_hooded", "name": "Lantern, hooded", "category": "gear",
            "cost_cp": 500, "weight_oz": 32, "hands": 1,
            "light": {"bright_ft": 30, "dim_ft": 30, "shape": "radius", "duration_rounds": 3600, "fuel": "test.v4.oil_flask",
                      "lit_by_default": False, "states": [{"id": "hooded", "bright_ft": 0, "dim_ft": 5, "toggle": "action"}]}}


def torch() -> dict:
    return {"$schema": "item@2", "id": "test.v4.torch", "name": "Torch", "category": "gear", "cost_cp": 1, "weight_oz": 16,
            "hands": 1, "light": {"bright_ft": 20, "dim_ft": 20, "duration_rounds": 600, "lit_by_default": True},
            "torch_attack": {"damage": [{"flat": 1, "type": "fire"}], "requires_lit": True}}


def arrows() -> dict:
    return {"$schema": "item@2", "id": "test.v4.arrow_1", "name": "Arrow, +1", "category": "ammunition", "ammo_kind": "arrow",
            "magic_bonus": 1, "cost_cp": 5, "weight_oz": 1}


def bow() -> dict:
    d = copy.deepcopy(core("items/longbow.json"))
    d.update({"$schema": "item@2", "id": "test.v4.longbow_split", "ammo_kind": "arrow", "attack_magic": 1, "damage_magic": 2})
    return d


def bugbear5() -> dict:
    """srd51.core bugbear (monster@5) with every new structural key and derive off (the derived-vs-printed check is
    DeriveCheck's)."""
    d = copy.deepcopy(core("monsters/bugbear.json"))
    assert d["$schema"] == "monster@5", d["$schema"]
    d.update({"$schema": "monster@5", "id": "test.v4.bugbear5", "derive": False, "hands": 2,
              "equipment": [{"item": "srd51.item.hide_armor", "equipped": True}, {"item": "srd51.item.shield", "equipped": True},
                            {"item": "test.v4.morningstar", "equipped": True}, {"item": "srd51.item.javelin", "count": 3}],
              "natural_armor": {"base": 11, "dex": True},
              "weapons": [{"id": "spiked_club", "name": "Spiked Club", "damage": [{"dice": "1d6", "type": "piercing"}], "reach": 5}],
              "emits_light": {"bright_ft": 0, "dim_ft": 5}})
    d.pop("proficiency_bonus")   # optional at @5: the CR table
    d["actions"][0]["weapon"] = "#spiked_club"
    return d


AT5_KEYS = ("derive", "equipment", "natural_armor", "ac_adjust", "weapon_dice_multiplier", "weapons", "hands",
            "proficiency_bonus_reason", "emits_light", "passive_perception_reason")


def as_monster4(d: dict) -> dict:
    """A monster@5 document with the @5 keys removed (the @4 shape it was converted from)."""
    d = copy.deepcopy(d)
    d["$schema"] = "monster@4"
    for k in AT5_KEYS:
        d.pop(k, None)
    for a in d["actions"]:
        for k in ("weapon", "natural", "attack_bonus", "damage_override", "use", "ability"):
            a.pop(k, None)
    for t in d.get("traits") or []:
        if isinstance(t.get("effect"), dict) and t["effect"].get("kind") == "weapon_extra_dice":
            del t["effect"]
    return d


def morningstar() -> dict:
    d = copy.deepcopy(core("items/morningstar.json"))
    d.update({"$schema": "item@2", "id": "test.v4.morningstar"})
    return d


def fighter5() -> dict:
    """srd51.core fighter_level_1 (character@5, derive on) plus a javelin stack."""
    d = copy.deepcopy(core("characters/fighter_level_1.json"))
    assert d["$schema"] == "character@5", d["$schema"]
    d["id"] = "test.v4.fighter5"
    d["equipment"] = d["equipment"] + [{"item": "srd51.item.javelin", "count": 4}]
    return d


def pack(**docs) -> dict:
    base = {"items/lantern.json": lantern(), "items/torch.json": torch(), "items/arrow.json": arrows(),
            "items/bow.json": bow(), "items/morningstar.json": morningstar(),
            "monsters/bugbear5.json": bugbear5(), "characters/fighter5.json": fighter5()}
    base.update(docs)
    return base


def errs_of(docs) -> list[str]:
    e, _w = h.validate_docs(docs)
    return e


class IdentityMigration(unittest.TestCase):
    def test_core_restamped_validates(self):
        """Every monster@4 / character@4 / item@1 file of srd51.core, re-stamped (identity migration), validates and gives the
        same findings as the original."""
        reg, by_tag = h.registry()
        with tempfile.TemporaryDirectory(prefix="equip_v5_") as tmp:
            dst = Path(tmp) / CORE.name
            shutil.copytree(CORE, dst)
            n = 0
            for f in dst.rglob("*.json"):
                d = json.loads(f.read_text(encoding="utf-8"))
                if isinstance(d, dict) and d.get("$schema") in RESTAMP:
                    d["$schema"] = RESTAMP[d["$schema"]]
                    f.write_text(json.dumps(d, indent=2), encoding="utf-8")
                    n += 1
            self.assertGreater(n, 30)   # the item@1 files (monsters and PCs are already @5)
            w_new: list[str] = []
            self.assertEqual(vp.validate_pack(dst, reg, by_tag, w_new), [])
        w_old: list[str] = []
        self.assertEqual(vp.validate_pack(CORE, reg, by_tag, w_old), [])
        self.assertEqual(len(w_new), len(w_old))

    def test_old_versions_reject_new_keys(self):
        old = as_monster4(core("monsters/bugbear.json"))
        self.assertEqual(errs_of({"monsters/b.json": dict(old, id="test.v4.b")}), [])
        old["derive"] = True
        self.assertTrue(any("derive" in e for e in errs_of({"monsters/b.json": dict(old, id="test.v4.b")})))
        it = dict(lantern(), **{"$schema": "item@1"})
        self.assertTrue(errs_of({"items/l.json": it}))


def schema(rel: str) -> dict:
    return json.loads((ROOT / "schemas" / rel).read_text(encoding="utf-8"))


class AppendOnly(unittest.TestCase):
    """The new versions carry the old ones in full: every old key keeps its definition; new keys are optional."""

    def test_properties_carried(self):
        for old, new, extra_req in (("item/v1", "item/v2", set()), ("monster/v4", "monster/v5", set()),
                                    ("character/v4", "character/v5", set())):
            o, n = schema(old + ".schema.json"), schema(new + ".schema.json")
            for k, v in o["properties"].items():
                if k in ("$schema", "category", "equipment"):
                    continue   # const bumped / enum widened / object arm widened (tested below)
                if new == "monster/v5" and k == "traits":   # v4's items plus the monster@5-local weapon_extra_dice arm
                    self.assertEqual(n["properties"][k]["items"]["allOf"][0], v["items"])
                    continue
                self.assertEqual(n["properties"][k], v, f"{new}: {k}")
            self.assertEqual(set(n["required"]), set(o["required"]) | extra_req, new)
        self.assertLessEqual(set(schema("item/v1.schema.json")["properties"]["category"]["enum"]),
                             set(schema("item/v2.schema.json")["properties"]["category"]["enum"]))

    def test_monster_stat_block_copy_matches_common2(self):
        """monster@5's own stat_block is common@2's (common@2 unchanged, EQUIPMENT 7.1) except actions[] items and the
        optional proficiency_bonus; its action def is common@2's plus weapon / natural / attack_bonus."""
        c2 = schema("common/v2.schema.json")["$defs"]
        m5 = schema("monster/v5.schema.json")["$defs"]
        canon = lambda d: json.dumps(d, sort_keys=True).replace("../common/v2.schema.json#/$defs/", "#/$defs/")
        self.assertEqual(set(m5["stat_block"]["properties"]), set(c2["stat_block"]["properties"]))
        for k, v in c2["stat_block"]["properties"].items():
            if k != "actions":
                self.assertEqual(canon(m5["stat_block"]["properties"][k]), canon(v), k)
        self.assertEqual(set(c2["stat_block"]["required"]) - set(m5["stat_block"]["required"]), {"proficiency_bonus"})
        self.assertEqual(set(m5["action"]["properties"]) - set(c2["action"]["properties"]), {"weapon", "natural", "attack_bonus", "damage_override", "use",
                          # batch 3 riders (monster@5 only)
                          "drain", "curse", "target_requires_any", "ability_drain", "feet", "with_attack",
                          "ability",   # CONTENT_CONVENTIONS "Attack ability"
                          "also_on_fail_by", "immune_on_success", "target_must_see_source"})   # Tier-A §26
        for k, v in c2["action"]["properties"].items():
            if k == "kind":   # monster@5 appends "teleport" (batch 3) then "area_effect"; common@2's enum is a strict prefix
                m_enum, c_enum = m5["action"]["properties"][k]["enum"], v["enum"]
                self.assertEqual(m_enum, c_enum + ["teleport", "area_effect"], k)
                continue
            if k == "damage":   # CONTENT_CONVENTIONS 19: items widened to monster_damage (common@2 damage | {flat, type})
                self.assertEqual(m5["action"]["properties"][k]["items"], {"$ref": "#/$defs/monster_damage"})
                self.assertEqual(m5["monster_damage"]["oneOf"][0], {"$ref": "../common/v2.schema.json#/$defs/damage"})
                self.assertEqual({x: y for x, y in m5["action"]["properties"][k].items() if x not in ("items", "description")},
                                 {x: y for x, y in v.items() if x not in ("items", "description")})
                continue
            self.assertEqual(canon(m5["action"]["properties"][k]), canon(v), k)


class TagTable(unittest.TestCase):
    def test_tag_table_matches_the_schemas(self):
        import checks_v4
        c2 = schema("common/v2.schema.json")["$defs"]
        for typ, sb in (("monster", schema("monster/v5.schema.json")["$defs"]["stat_block"]),
                        ("character", schema("character/v5.schema.json")["$defs"]["stat_block"])):
            tag = f"{typ}@5"
            declared = set(sb["properties"]) | set(schema(f"{typ}/v5.schema.json")["properties"])
            self.assertEqual(set(checks_v4.TAG_TABLE[tag].top_level_keys), declared, tag)
            r4, r5 = checks_v4.TAG_TABLE[f"{typ}@4"], checks_v4.TAG_TABLE[tag]
            local = checks_v4.MONSTER_AT5_EFFECT_KINDS if typ == "monster" else frozenset()
            local_act = checks_v4.MONSTER_AT5_ACTION_KINDS if typ == "monster" else frozenset()
            self.assertEqual((r4.action_kinds | local_act, r4.effect_kinds | local, r4.cost_values), (r5.action_kinds, r5.effect_kinds, r5.cost_values))
        self.assertEqual(checks_v4.MONSTER_AT5_EFFECT_KINDS, {"weapon_extra_dice", "save_advantage", "aura", "keen_senses"})
        self.assertEqual(checks_v4.MONSTER_AT5_ACTION_KINDS, {"teleport", "area_effect"})


class LightPresetAndVisual(unittest.TestCase):
    """item@2 light.preset (Render's preset key) and the presentation-only `visual` block (item@2 is a superset of item@1)."""

    VIS = {"color": {"r": 255, "g": 170, "b": 70}, "intensity": 1000, "flicker": "torch"}

    def test_valid(self):
        t = torch(); t["light"]["preset"] = "srd51.core.light.torch"; t["visual"] = self.VIS
        self.assertEqual(errs_of(pack(**{"items/torch.json": t})), [])
        for doc in (bow(), arrows(), morningstar()):
            self.assertEqual(errs_of(pack(**{"items/x.json": dict(doc, visual=self.VIS)})), [], doc["id"])

    def test_bad(self):
        for preset in ("Torch", "torch", "srd51..light", 7):
            t = torch(); t["light"]["preset"] = preset
            self.assertTrue(errs_of(pack(**{"items/torch.json": t})), preset)
        for vis in ({"color": {"r": 256, "g": 0, "b": 0}, "intensity": 1}, {"color": {"r": 1, "g": 1, "b": 1}},
                    dict(VIS, intensity=1001), dict(VIS, flicker="strobe"), dict(VIS, extra=1), dict(VIS, intensity="1")):
            self.assertTrue(errs_of(pack(**{"items/torch.json": dict(torch(), visual=vis)})), vis)

    def test_visual_matches_item1_when_present(self):
        v1 = schema("item/v1.schema.json")["properties"].get("visual")
        if v1 is not None:   # item@1 gains it with Render #233
            self.assertEqual(schema("item/v2.schema.json")["properties"]["visual"], v1)


VIS = LightPresetAndVisual.VIS


def pouch() -> dict:
    return {"$schema": "item@2", "id": "test.v4.pouch", "name": "Pouch", "category": "gear", "cost_cp": 50, "weight_oz": 16,
            "container": {"holds": [{"ammo_kind": "sling_bullet", "capacity": 20}, {"ammo_kind": "needle", "capacity": 50}]}}


class Container(unittest.TestCase):
    def test_valid(self):
        self.assertEqual(errs_of(pack(**{"items/pouch.json": pouch()})), [])

    def test_bad(self):
        for c in ({}, {"holds": []}, {"holds": [{"ammo_kind": "arrow"}]}, {"holds": [{"ammo_kind": "rock", "capacity": 1}]},
                  {"holds": [{"ammo_kind": "arrow", "capacity": 0}]}, {"holds": [{"ammo_kind": "arrow", "capacity": "20"}]},
                  {"holds": [{"ammo_kind": "arrow", "capacity": 20}], "extra": 1}):
            self.assertTrue(errs_of(pack(**{"items/pouch.json": dict(pouch(), container=c)})), c)
        self.assertTrue(errs_of(pack(**{"items/arrow.json": dict(arrows(), container=pouch()["container"])})))   # gear only
        dup = pouch(); dup["container"]["holds"].append({"ammo_kind": "needle", "capacity": 10})
        e = errs_of(pack(**{"items/pouch.json": dup}))
        self.assertTrue(any("/container/holds: ammo_kind values must be unique" in x for x in e), e)


class NewShapes(unittest.TestCase):
    def test_full_pack_valid(self):
        e, w = h.validate_docs(pack())
        self.assertEqual(e, [])

    def test_closed_arms(self):
        self.assertTrue(errs_of(pack(**{"items/arrow.json": dict(arrows(), hands=1)})))
        self.assertTrue(errs_of(pack(**{"items/torch.json": dict(torch(), reach=5)})))
        self.assertTrue(errs_of(pack(**{"items/arrow.json": h.edit(arrows(), dels=[["ammo_kind"]])})))
        bad = lantern(); bad["light"]["radius_ft"] = 10
        self.assertTrue(errs_of(pack(**{"items/lantern.json": bad})))
        bad = lantern(); bad["light"]["dim_ft"] = 7   # integer multiples of 5
        self.assertTrue(errs_of(pack(**{"items/lantern.json": bad})))
        b = bugbear5(); b["actions"][0]["natural"] = {"dice": "2d8", "ability": "cha"}
        self.assertTrue(errs_of(pack(**{"monsters/bugbear5.json": b})))
        b = bugbear5(); b["hands"] = 1
        self.assertTrue(errs_of(pack(**{"monsters/bugbear5.json": b})))
        f = fighter5(); f["equipment"][-1]["hand"] = "left"
        self.assertTrue(errs_of(pack(**{"characters/fighter5.json": f})))

    def test_character_pb_still_required(self):
        self.assertTrue(errs_of(pack(**{"characters/fighter5.json": h.edit(fighter5(), dels=[["proficiency_bonus"]])})))


BRUTE = {"kind": "weapon_extra_dice", "dice": 1, "melee_only": True}


class WeaponExtraDice(unittest.TestCase):
    """monster@5-local trait kind weapon_extra_dice (Brute; EQUIPMENT 5.3, #247 MonsterSpec::brute_dice). common@2 is unchanged,
    so @4 documents and characters keep the open-kind behaviour (unknown-kind warning, never loaded)."""

    def brute(self, eff, schema="monster@5"):
        b = bugbear5() if schema == "monster@5" else dict(as_monster4(core("monsters/bugbear.json")), id="test.v4.bugbear5")
        i = next(i for i, t in enumerate(b["traits"]) if t["id"] == "brute")
        b["traits"][i]["effect"] = eff
        return h.validate_docs(pack(**{"monsters/bugbear5.json": b}))

    def test_valid(self):
        for eff in (BRUTE, {"kind": "weapon_extra_dice", "dice": 2}):
            e, w = self.brute(eff)
            self.assertEqual(e, [], eff)
            self.assertFalse(any("weapon_extra_dice" in x for x in w), w)

    def test_closed_arm(self):
        for eff in (dict(BRUTE, dice=0), dict(BRUTE, dice=5), dict(BRUTE, dice="1d8"), {"kind": "weapon_extra_dice"},
                    dict(BRUTE, ranged=True), dict(BRUTE, melee_only="yes")):
            e, _w = self.brute(eff)
            self.assertTrue(any("/traits/" in x for x in e), (eff, e))

    def test_not_a_kind_outside_monster5(self):
        e, w = self.brute(BRUTE, schema="monster@4")
        self.assertEqual(e, [])
        self.assertTrue(any("unknown effect kind 'weapon_extra_dice'" in x for x in w), w)
        f = fighter5(); f["features"] = list(f.get("features") or []) + [{"id": "brute", "name": "Brute", "effect": BRUTE}]
        e, w = h.validate_docs(pack(**{"characters/fighter5.json": f}))
        self.assertEqual(e, [])
        self.assertTrue(any("unknown effect kind 'weapon_extra_dice'" in x for x in w), w)


BITE = {"id": "bite", "name": "Bite", "kind": "melee_weapon_attack", "to_hit": 0, "reach": 5, "targets": 1,
        "damage": [{"flat": 1, "type": "piercing"}], "description": "Melee Weapon Attack: +0 to hit, reach 5 ft., one target. Hit: 1 piercing damage."}


class FlatDamage(unittest.TestCase):
    """monster@5-local $defs/monster_damage (CONTENT_CONVENTIONS 19): {flat, type} or common@2 damage, exactly one (oneOf).
    SRD 'Hit: 1 piercing damage' (rat, cat, scorpion). The loader maps flat to DiceExpr{0, 0, N} (sim flat_damage_tests)."""

    def with_bite(self, damage, schema="monster@5"):
        b = bugbear5() if schema == "monster@5" else dict(as_monster4(core("monsters/bugbear.json")), id="test.v4.bugbear5")
        b["actions"] = list(b["actions"]) + [dict(BITE, damage=damage)]
        return h.validate_docs(pack(**{"monsters/bugbear5.json": b}))

    def test_valid(self):
        for dmg in ([{"flat": 1, "type": "piercing"}], [{"flat": 1, "type": "piercing"}, {"dice": "3d4", "type": "poison"}],
                    [{"dice": "1d4+2", "type": "piercing"}]):
            e, _w = self.with_bite(dmg)
            self.assertEqual(e, [], dmg)

    def test_bad(self):
        for dmg in ([{"flat": 0, "type": "piercing"}], [{"flat": 256, "type": "piercing"}], [{"flat": "1", "type": "piercing"}],
                    [{"flat": 1, "dice": "1d4", "type": "piercing"}], [{"flat": 1}],
                    [{"flat": 1, "type": "piercing", "bonus_vs": {"creature_types": ["undead"], "dice": "1d6"}}],
                    [{"flat": 1, "type": "piercing", "modifier": 2}]):
            e, _w = self.with_bite(dmg)
            self.assertTrue(any("/damage/" in x for x in e), (dmg, e))

    def cat(self) -> dict:
        return json.loads((FLAT_FX / "monsters" / "cat.json").read_text(encoding="utf-8"))

    def test_cat_fixture_validates(self):
        e, w = h.validate_docs({"monsters/cat.json": self.cat()})
        self.assertEqual(e, [])

    def test_cat_in_a_real_pack(self):
        """The fixture dropped into a copy of srd51.core as monsters/cat.json validates with validate_pack (derive on)."""
        reg, by_tag = h.registry()
        with tempfile.TemporaryDirectory(prefix="flat_cat_") as tmp:
            dst = Path(tmp) / CORE.name
            shutil.copytree(CORE, dst)
            (dst / "monsters" / "cat.json").write_text(json.dumps(dict(self.cat(), id="srd51.monster.cat"), indent=2), encoding="utf-8")
            man = json.loads((dst / "manifest.json").read_text(encoding="utf-8"))
            man["contents"] = sorted(set(man["contents"]) | {"monsters/cat.json"})   # union: no duplicate once the pack ships cat.json
            (dst / "manifest.json").write_text(json.dumps(man, indent=2), encoding="utf-8")
            warns: list[str] = []
            self.assertEqual(vp.validate_pack(dst, reg, by_tag, warns), [])

    def test_natural_flat_shape(self):
        def errs(nat, dmg=None):
            c = self.cat(); c["actions"][0]["natural"] = nat
            if dmg is not None:
                c["actions"][0]["damage"] = dmg
            return h.validate_docs({"monsters/cat.json": c})[0]
        self.assertEqual(errs({"flat": 1, "ability": "dex"}), [])
        for nat in ({"flat": 1, "dice": "1d4", "ability": "dex"}, {"flat": 0, "ability": "dex"}, {"flat": 256, "ability": "dex"},
                    {"flat": 1, "ability": "dex", "damage_ability": "none"}, {"flat": 1}, {"dice": "1", "ability": "dex"}, {"ability": "dex"}):
            self.assertTrue(errs(nat), nat)
        # natural.flat must equal the flat damage[0]
        self.assertTrue(any("natural.flat 2 != damage[0] flat 1" in x for x in errs({"flat": 2, "ability": "dex"})))
        self.assertTrue(any("natural.flat 1 != damage[0] flat none" in x
                            for x in errs({"flat": 1, "ability": "dex"}, [{"dice": "1d4", "type": "slashing"}])))

    def test_not_in_monster4(self):
        e, _w = self.with_bite([{"flat": 1, "type": "piercing"}], schema="monster@4")
        self.assertTrue(e)


def mon(lid: str) -> dict:
    d = copy.deepcopy(core(f"monsters/{lid}.json"))
    d["id"] = f"test.v4.{lid}"
    return d


def act(d: dict, aid: str) -> dict:
    return next(a for a in d["actions"] if a["id"] == aid)


class DeriveCheck(unittest.TestCase):
    """EQUIPMENT 5.6 derived-vs-printed, mirroring rules5e::derive_monster's strings (engine/rules/src/equipment.cpp)."""

    def run_one(self, d):
        return h.validate_docs({f"monsters/{d['id'].split('.')[-1]}.json": d})

    def expect(self, d, msg):
        e, _w = self.run_one(d)
        self.assertIn(msg, [x.split(": ", 1)[1] if x.startswith("monsters/") else x for x in e], e)

    def test_srd51_core_monsters_all_derive(self):
        n = 0
        for f in sorted((CORE / "monsters").glob("*.json")):
            d = json.loads(f.read_text(encoding="utf-8"))
            if d["$schema"] == "monster@5":
                n += 1
                self.assertIs(d["derive"], True, f.name)
                for a in d["actions"]:
                    if a["kind"] in ("melee_weapon_attack", "ranged_weapon_attack"):
                        flat = all("flat" in c for c in a["damage"][:1])   # CONTENT_CONVENTIONS 19: no die to derive from
                        self.assertTrue("weapon" in a or "natural" in a or flat, f"{f.name}: {a['id']}")
        self.assertEqual(n, 163)   # 33 converted (PR B) + 9 bestiary v0.1 batch 1 + 9 batch 2 + rat, scorpion (flat) + satyr, ghast (batch 3) + gladiator (riders, §21) + 5 batch 3b (blink dog deferred) + 11 tier B batch d1 + 10 batch D2 + 3 flat-damage batch d3 + acolyte, cult fanatic (casters) + blink dog, mummy (Tier-A b3c) + 6 CR 0 beasts (badger, eagle, owl, quipper, raven, vulture) + 4 CR 1/8 beasts (blood hawk, flying snake, giant crab, stirge) + 5 CR 1/4 batch B (constrictor snake, elk, panther, swarm of bats, swarm of ravens) + 4 CR 1/4 batch A beasts (giant badger, giant bat, giant frog, giant owl) + 4 CR 1/4 batch C (drow, pseudodragon, sprite, steam mephit) + 5 CR 1/2 batch D (giant goat, giant sea horse, giant wasp, swarm of insects, warhorse) + 5 CR 1/2 batch E (ice mephit, dust mephit, magma mephit, cockatrice, sahuagin) + 5 CR 1/2 batch F (crocodile, darkmantle, rust monster, gray ooze, deep gnome) + 5 CR 1 batch G (lion, tiger, giant eagle, giant vulture, hippogriff) + 6 CR 1 batch H (harpy, dryad, duergar, specter, giant octopus, swarm of quippers) + 6 CR 2 batch J (centaur, griffon, pegasus, ankheg, giant constrictor snake, minotaur skeleton) + 6 CR 2 batch I (polar bear, giant boar, giant elk, rhinoceros, saber-toothed tiger, hunter shark) + 4 CR 2 batch K (druid, priest, merrow, wererat) + 5 CR 2 batch L (grick, swarm of poisonous snakes, gibbering mouther, will-o'-wisp, ochre jelly) + 4 CR 3 batch M (giant scorpion, minotaur, hell hound, winter wolf)
        self.assertEqual(core("monsters/goblin.json")["$schema"], "monster@3")   # fl1 compat: left at @3

    def test_valid_conversions(self):
        for lid in ("ogre", "bugbear", "skeleton", "black_bear", "ghoul", "lizardfolk", "gelatinous_cube", "grimlock"):
            e, w = self.run_one(mon(lid))
            self.assertEqual(e, [], lid)

    def test_adjustments_are_listed(self):
        _e, w = self.run_one(mon("black_bear"))
        self.assertEqual(sum("attack_bonus 3 (" in x for x in w), 2, w)
        _e, w = self.run_one(mon("skeleton"))
        self.assertTrue(any("test.v4.skeleton: ac_adjust 1 (" in x for x in w), w)

    def test_damage_override(self):
        # CONTENT_CONVENTIONS 21: an explicit printed damage[0] with a reason (SRD Gladiator thrown Spear 2d6 + 4). Brute adds
        # its die on melee use only, so a thrown line printed with the extra die needs the override; bugbear stays as printed.
        d = mon("bugbear"); a = act(d, "javelin_thrown")
        a["damage"][0]["dice"] = "2d6+2"
        self.expect(d, "test.v4.bugbear.javelin_thrown: derived damage 1d6+2 != printed 2d6+2 (weapon_dice_multiplier 2 would match)")
        a["damage_override"] = {"dice": "2d6+2", "reason": "test: printed with the Brute die at range"}
        e, w = self.run_one(d)
        self.assertEqual(e, [])
        self.assertTrue(any("test.v4.bugbear.javelin_thrown: damage_override 2d6+2 (" in x for x in w), w)
        a["damage_override"]["reason"] = "  "
        self.expect(d, "test.v4.bugbear.javelin_thrown: damage_override needs a reason")
        d = mon("bugbear"); act(d, "javelin_thrown")["damage_override"] = {"dice": "1d6+2", "reason": "x"}
        self.expect(d, "test.v4.bugbear.javelin_thrown: damage_override is not needed")
        d = mon("bugbear"); act(d, "javelin_thrown")["damage_override"] = {"dice": "1d6+2"}
        e, _w = self.run_one(d)
        self.assertTrue(any("'reason' is a required property" in x for x in e), e)

    def test_ac_mismatch(self):
        d = mon("guard"); d["armor_class"]["value"] = 17
        self.expect(d, "test.v4.guard: derived AC 16 != printed 17")

    def test_to_hit_mismatch(self):
        d = mon("orc"); act(d, "greataxe")["to_hit"] = 6
        self.expect(d, "test.v4.orc.greataxe: derived to-hit +5 != printed +6")

    def test_damage_mismatch_and_multiplier_hint(self):
        d = mon("orc"); act(d, "greataxe")["damage"][0]["dice"] = "1d10+3"
        self.expect(d, "test.v4.orc.greataxe: derived damage 1d12+3 != printed 1d10+3")
        d = mon("ogre"); del d["weapon_dice_multiplier"]
        self.expect(d, "test.v4.ogre.greatclub: derived damage 1d8+4 != printed 2d8+4 (weapon_dice_multiplier 2 would match)")

    def test_not_needed(self):
        d = mon("guard"); d["ac_adjust"] = {"value": 0, "reason": "test"}
        self.expect(d, "test.v4.guard: ac_adjust is not needed")
        d = mon("orc"); act(d, "greataxe")["attack_bonus"] = {"value": 5, "reason": "test"}
        self.expect(d, "test.v4.orc.greataxe: attack_bonus is not needed")
        d = mon("orc"); d["weapon_dice_multiplier"] = 2
        self.expect(d, "test.v4.orc: weapon_dice_multiplier is not needed")
        d = mon("orc"); d["natural_armor"] = {"base": 11}
        self.expect(d, "test.v4.orc: natural_armor is not needed")
        d = mon("zombie"); d["natural_armor"] = {"base": 8}   # 10 + Dex -2 = 8 already
        self.expect(d, "test.v4.zombie: natural_armor is not needed")

    def test_missing_reasons(self):
        d = mon("skeleton"); d["ac_adjust"]["reason"] = "  "
        self.expect(d, "test.v4.skeleton: ac_adjust needs a reason")
        d = mon("ghoul"); act(d, "bite")["attack_bonus"]["reason"] = "\t"
        self.expect(d, "test.v4.ghoul.bite: attack_bonus needs a reason")

    def test_pb_off_table(self):
        d = mon("orc"); d["proficiency_bonus"] = 3
        self.expect(d, "test.v4.orc: proficiency_bonus 3 != CR table 2")

    def test_weapon_use_errors(self):
        d = mon("orc"); del act(d, "greataxe")["use"]
        self.expect(d, "test.v4.orc.greataxe: weapon: a two-handed weapon needs both hands")
        d = mon("orc"); act(d, "greataxe")["use"] = "thrown"
        self.expect(d, "test.v4.orc.greataxe: weapon: not a thrown weapon")
        d = mon("bugbear"); act(d, "morningstar")["use"] = "two_hands"
        self.expect(d, "test.v4.bugbear.morningstar: weapon: not a versatile weapon")
        d = mon("bugbear"); act(d, "morningstar")["use"] = "off_hand"
        self.expect(d, "test.v4.bugbear.morningstar: weapon: the off-hand weapon must be a light melee weapon")

    def test_derive_off_is_not_checked(self):
        d = mon("guard"); d["armor_class"]["value"] = 17; d["derive"] = False
        e, _w = self.run_one(d)
        self.assertEqual(e, [])


DB_WEAPON = {"simple-weapons": "simple", "martial-weapons": "martial", "hand-crossbows": "srd51.item.crossbow_hand",
             "crossbows-light": "srd51.item.crossbow_light", "light-hammers": "srd51.item.light_hammer"}
DB_ARMOR = {"light-armor": {"light"}, "medium-armor": {"medium"}, "all-armor": {"light", "medium", "heavy"}, "shields": {"shields"}}
AMMO_ITEM = {"arrow": ("srd51.item.arrow", 20), "bolt": ("srd51.item.crossbow_bolt", 20),
             "sling_bullet": ("srd51.item.sling_bullet", 20), "needle": ("srd51.item.blowgun_needle", 50)}


def db_weapon(idx: str) -> str:
    return DB_WEAPON.get(idx, "srd51.item." + idx[:-1].replace("-", "_"))   # 'longswords' -> srd51.item.longsword


def pc(lid: str) -> dict:
    d = copy.deepcopy(core(f"characters/{lid}.json"))
    d["id"] = f"test.v4.{lid}"
    return d


class PcConversion(unittest.TestCase):
    """PR C: the 63 srd51.core PCs at character@5 with derive on (EQUIPMENT 2.1, 2.5, 3.1, 7.2)."""

    def test_proficiency_table_matches_5e_database(self):
        import checks_v4
        db = json.loads((ROOT / "tests/validate/data/srd51_proficiencies_5e_database_a6212be.json").read_text(encoding="utf-8"))
        for cls, profs in db["classes"].items():
            w = {db_weapon(p) for p in profs if p not in DB_ARMOR}
            a = set().union(*[DB_ARMOR[p] for p in profs if p in DB_ARMOR])
            self.assertEqual(checks_v4.SRD_CLASS_PROFICIENCIES[cls], (w, a), cls)
        self.assertEqual(set(checks_v4.SRD_CLASS_PROFICIENCIES), set(db["classes"]))
        dwarf = {db_weapon(p) for p in db["traits"]["dwarven-combat-training"]["proficiencies"]}
        elf = {db_weapon(p) for p in db["traits"]["elf-weapon-training"]["proficiencies"]}
        self.assertEqual(checks_v4.SRD_RACE_WEAPON_PROFICIENCIES, {"dwarf": dwarf, "hill_dwarf": dwarf, "high_elf": elf})

    def test_all_pcs_converted(self):
        n = 0
        for f in sorted((CORE / "characters").glob("*.json")):
            d = json.loads(f.read_text(encoding="utf-8"))
            n += 1
            self.assertEqual((d["$schema"], d["derive"]), ("character@5", True), f.name)
            self.assertIn("weapon_proficiencies", d)
            self.assertIn("armor_proficiencies", d)
            items = {e["item"]: e for e in d["equipment"] if isinstance(e, dict)}
            for e in items.values():
                if e.get("equipped"):
                    cat = json.loads((CORE / "items" / (e["item"].split(".")[-1] + ".json")).read_text())["category"]
                    if cat in ("weapon", "shield"):
                        self.assertEqual(e.get("hand"), "off" if cat == "shield" else "main", f"{f.name}: {e}")
            for a in d["actions"]:
                if a["kind"] in ("melee_weapon_attack", "ranged_weapon_attack") and a["id"] != "unarmed_strike":
                    self.assertIn(a.get("weapon"), items, f"{f.name}: {a['id']}")
            # SRD starting ammunition: 20 arrows / bolts / sling bullets, 50 needles, for every ammunition weapon carried
            self.assertFalse([e for e in d["equipment"] if isinstance(e, str) and e in ("arrows_20", "crossbow_bolts_20")])
            for iid in items:
                it = json.loads((CORE / "items" / (iid.split(".")[-1] + ".json")).read_text())
                if it.get("ammo_kind") and it["category"] == "weapon":
                    stack, count = AMMO_ITEM[it["ammo_kind"]]
                    self.assertEqual(items.get(stack, {}).get("count"), count, f"{f.name}: {iid}")
        self.assertEqual(n, 63)

    def test_sample_pack_pc_left_at_3(self):
        sample = ROOT / "content/packs/sample.vertical_slice"
        tags = {json.loads(f.read_text())["$schema"] for f in sample.glob("characters/*.json")}
        self.assertEqual(tags, {"character@3"})

    def test_no_proficiency_warnings(self):
        _e, w = h.validate_docs({"characters/x.json": pc("sample_high_elf_wizard_1")})
        self.assertFalse([x for x in w if "proficienc" in x], w)

    def expect(self, d, msg, warn=False):
        e, w = h.validate_docs({f"characters/{d['id'].split('.')[-1]}.json": d})
        pool = w if warn else e
        self.assertTrue(any(msg in x for x in pool), (msg, e, w))

    def test_proficiency_findings(self):
        d = pc("fighter_level_1"); d["armor_proficiencies"].remove("heavy")
        self.expect(d, "test.v4.fighter_level_1: armor_proficiencies missing 'heavy' (SRD 5.1 class / race)")
        d = pc("sample_hill_dwarf_cleric_1"); d["weapon_proficiencies"] = ["simple"]
        self.expect(d, "test.v4.sample_hill_dwarf_cleric_1: weapon_proficiencies missing 'srd51.item.battleaxe' (SRD 5.1 class / race)")
        d = pc("wizard_level_1"); d["weapon_proficiencies"].append("martial")
        self.expect(d, "test.v4.wizard_level_1: weapon_proficiencies 'martial' is not granted by the SRD 5.1 class / race", warn=True)

    def test_derive_mismatches(self):
        d = pc("barbarian_level_1"); d["armor_class"]["value"] = 13
        self.expect(d, "test.v4.barbarian_level_1: derived AC 14 != printed 13")
        d = pc("fighter_level_5"); act(d, "longsword")["to_hit"] = 8
        self.expect(d, "test.v4.fighter_level_5.longsword: derived to-hit +7 != printed +8")
        d = pc("fighter_level_5"); act(d, "longsword")["damage"][0]["dice"] = "1d8+6"   # Dueling is the engine's, never baked
        self.expect(d, "test.v4.fighter_level_5.longsword: derived damage 1d8+4 != printed 1d8+6")
        d = pc("barbarian_level_1"); del act(d, "greataxe")["use"]
        self.expect(d, "test.v4.barbarian_level_1.greataxe: weapon: a two-handed weapon needs both hands")
        d = pc("cleric_level_1"); d["weapon_proficiencies"] = []   # not proficient: no PB on the mace (and the missing error)
        self.expect(d, "test.v4.cleric_level_1.mace: derived to-hit +2 != printed +4")
        d = pc("wizard_level_1"); act(d, "dagger")["weapon"] = "srd51.item.rapier"
        self.expect(d, "/actions/0/weapon: 'srd51.item.rapier' is not listed in equipment[]")

    def test_monk_shield_loses_unarmored_defense(self):
        d = pc("monk_level_5")
        d["equipment"].append({"item": "srd51.item.shield", "equipped": True, "hand": "off"})
        # 10 + Dex 4 + shield 2 = 16 (Monk Unarmored Defense is ineligible with a shield): equals the printed 16 by coincidence,
        # so print 18 (what stacking would give) and expect the mismatch.
        d["armor_class"]["value"] = 18
        self.expect(d, "test.v4.monk_level_5: derived AC 16 != printed 18")


class StructuralChecks(unittest.TestCase):
    def expect(self, docs, msg):
        e = errs_of(docs)
        self.assertTrue(any(msg in x for x in e), f"{msg!r} not in {e}")

    def test_pb_table(self):
        b = bugbear5(); b["proficiency_bonus"] = 3   # CR 1
        self.expect(pack(**{"monsters/bugbear5.json": b}), "test.v4.bugbear5: proficiency_bonus 3 != CR table 2")
        b["proficiency_bonus_reason"] = "authored deviation (test)"
        e = errs_of(pack(**{"monsters/bugbear5.json": b}))
        self.assertFalse(any("proficiency_bonus" in x for x in e), e)

    def test_passive_perception_reason(self):   # Tier-A §25 (Lich ruling): mismatch = error unless srd_printed (then a warning)
        b = bugbear5(); b["senses"]["passive_perception"] += 3
        self.expect(pack(**{"monsters/bugbear5.json": b}), "senses.passive_perception")
        b["passive_perception_reason"] = "srd_printed"
        e, w = h.validate_docs(pack(**{"monsters/bugbear5.json": b}))
        self.assertFalse(any("passive_perception" in x for x in e), e)
        self.assertTrue(any("SRD printed value used" in x for x in w), w)
        b = bugbear5(); b["passive_perception_reason"] = "srd_printed"   # no mismatch: the reason is refused
        self.expect(pack(**{"monsters/bugbear5.json": b}), "passive_perception_reason given but")

    def test_dreadful_glare_keys(self):   # Tier-A §26 (Lich ruling): save_effect riders, monster@5 only
        glare = {"kind": "save_effect", "id": "dreadful_glare", "name": "Dreadful Glare", "magical": True, "range_ft": 60,
                 "save": {"ability": "wis", "dc": 11},
                 "apply": [{"condition": "frightened", "duration": {"kind": "until_end_of_caster_next_turn"}}],
                 "target_must_see_source": True, "also_on_fail_by": {"margin": 5, "condition": "paralyzed"},
                 "immune_on_success": {"hours": 24, "scope": "same_action"}}
        b = bugbear5(); b["actions"].append(copy.deepcopy(glare))
        e, _w = h.validate_docs(pack(**{"monsters/bugbear5.json": b}))
        self.assertEqual(e, [])
        b = bugbear5(); g = copy.deepcopy(glare); g["also_on_fail_by"]["condition"] = "frightened"; b["actions"].append(g)
        self.expect(pack(**{"monsters/bugbear5.json": b}), "must differ from the primary condition")
        b = bugbear5(); g = copy.deepcopy(glare); g["also_on_fail_by"]["margin"] = 0; b["actions"].append(g)
        self.expect(pack(**{"monsters/bugbear5.json": b}), "also_on_fail_by")
        b = bugbear5(); g = copy.deepcopy(glare); g["immune_on_success"]["scope"] = "all"; b["actions"].append(g)
        self.expect(pack(**{"monsters/bugbear5.json": b}), "immune_on_success")
        b = bugbear5(); g = copy.deepcopy(glare); g["apply"][0]["repeat_save"] = "end_of_turn"; b["actions"].append(g)
        e, w = h.validate_docs(pack(**{"monsters/bugbear5.json": b}))
        self.assertTrue(any("ends both conditions" in x for x in w), w)
        import checks_v4 as c4
        e, _w = c4.check_save_effect_riders({"id": "x", "actions": [copy.deepcopy(glare)]}, "monster@4")
        self.assertTrue(any("need a monster@5 file" in x for x in e), e)

    def test_reasons(self):
        b = bugbear5(); b["ac_adjust"] = {"value": 1, "reason": " "}
        self.expect(pack(**{"monsters/bugbear5.json": b}), "test.v4.bugbear5: ac_adjust needs a reason")
        b = bugbear5(); b["actions"][1]["attack_bonus"] = {"value": 3, "reason": "  "}
        aid = b["actions"][1]["id"]
        self.expect(pack(**{"monsters/bugbear5.json": b}), f"test.v4.bugbear5.{aid}: attack_bonus needs a reason")

    def test_adjustments_are_listed(self):
        b = bugbear5(); b["ac_adjust"] = {"value": 1, "reason": "armor scraps"}
        e, w = h.validate_docs(pack(**{"monsters/bugbear5.json": b}))
        self.assertEqual(e, [])
        self.assertTrue(any("test.v4.bugbear5: ac_adjust 1 (armor scraps)" in x for x in w), w)

    def test_hands(self):
        b = bugbear5(); b["equipment"].append({"item": "test.v4.torch", "equipped": True})
        self.expect(pack(**{"monsters/bugbear5.json": b}), "test.v4.bugbear5: holds 3 items but has 2 hands")
        b["hands"] = 0; b["equipment"] = b["equipment"][:2]
        self.expect(pack(**{"monsters/bugbear5.json": b}), "test.v4.bugbear5: holds 1 items but has 0 hands")
        f = fighter5(); f["equipment"].append({"item": "srd51.item.dagger", "hand": "main"})
        self.expect(pack(**{"characters/fighter5.json": f}), "test.v4.fighter5: hand 'main' holds 2 items")

    def test_weapon_links(self):
        b = bugbear5(); b["actions"][0]["weapon"] = "#nope"
        self.expect(pack(**{"monsters/bugbear5.json": b}), "/actions/0/weapon: '#nope' is not a weapons[] id of this document")
        b = bugbear5(); b["actions"][0]["weapon"] = "srd51.item.greatclub"
        self.expect(pack(**{"monsters/bugbear5.json": b}), "/actions/0/weapon: 'srd51.item.greatclub' is not listed in equipment[]")
        b = bugbear5(); b["actions"][0]["natural"] = {"dice": "2d8", "ability": "str"}
        self.expect(pack(**{"monsters/bugbear5.json": b}), "/actions/0: 'weapon' and 'natural' are exclusive")
        b = bugbear5(); b["weapons"].append(copy.deepcopy(b["weapons"][0]))
        self.expect(pack(**{"monsters/bugbear5.json": b}), "/weapons: local weapon ids must be unique")

    def test_light(self):
        bad = lantern(); bad["light"]["bright_ft"] = 100
        self.expect(pack(**{"items/lantern.json": bad}), "/light: bright_ft + dim_ft 130 exceeds 120 (dim is additional)")
        bad = lantern(); bad["light"]["states"].append({"id": "hooded", "bright_ft": 0, "dim_ft": 0})
        self.expect(pack(**{"items/lantern.json": bad}), "/light/states: state ids must be unique")
        bad = lantern(); bad["light"].update({"shape": "cone", "bright_ft": 0})
        self.expect(pack(**{"items/lantern.json": bad}), "/light: a cone's length is bright_ft, which must be above 0")
        b = bugbear5(); b["emits_light"] = {"bright_ft": 60, "dim_ft": 65}
        self.expect(pack(**{"monsters/bugbear5.json": b}), "/emits_light: bright_ft + dim_ft 125 exceeds 120")

    def test_ammo_kind_needs_ammunition_property(self):
        d = copy.deepcopy(core("items/longsword.json")); d.update({"$schema": "item@2", "id": "test.v4.ls", "ammo_kind": "arrow"})
        self.expect(pack(**{"items/ls.json": d}), "/ammo_kind: only a weapon with the 'ammunition' property fires ammunition")


if __name__ == "__main__":
    unittest.main(verbosity=1)
