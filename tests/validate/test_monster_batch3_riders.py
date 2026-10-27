#!/usr/bin/env python3
"""Batch 3 small monster riders (ctest: content_validate_batch3_riders): monster@5 schema arms and the validator mirrors of the
sim loader refusals. Lich rulings appended to batch3_gaps_proposal.md; SRD 5.1 wording only."""
from __future__ import annotations

import copy
import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import v4_helpers as h  # noqa: E402

CORE = h.ROOT / "content" / "packs" / "srd51.core"


def bugbear() -> dict:
    d = json.loads((CORE / "monsters" / "bugbear.json").read_text(encoding="utf-8"))
    assert d["$schema"] == "monster@5"
    d["id"] = "test.v4.b3_bugbear"
    d["derive"] = False   # the derived-vs-printed AC check needs the item ids; not under test here
    return d


def with_trait(effect: dict, *more: dict, schema: str = "monster@5") -> dict:
    d = bugbear()
    d["$schema"] = schema
    for i, e in enumerate((effect,) + more):
        d["traits"].append({"id": f"sa_{i}", "name": "Magic Resistance", "description": "test", "effect": e})
    return d


def run(doc: dict) -> tuple[list[str], list[str]]:
    return h.validate_in_memory(doc, "monsters/b3.json")


class SaveAdvantage(unittest.TestCase):
    def test_valid(self):
        for eff in ({"kind": "save_advantage", "magical": True},
                    {"kind": "save_advantage", "save_vs_conditions": ["charmed", "frightened"]},
                    {"kind": "save_advantage", "vs_damage_types": ["poison"], "abilities": ["con"], "mode": "disadvantage"}):
            e, w = run(with_trait(eff))
            self.assertEqual(e, [], eff)
            self.assertFalse([m for m in w if "save_advantage" in m], w)

    def test_refused(self):
        for eff, frag in (({"kind": "save_advantage"}, ""),
                          ({"kind": "save_advantage", "magical": False}, ""),
                          ({"kind": "save_advantage", "save_vs_conditions": []}, ""),
                          ({"kind": "save_advantage", "magical": True, "note": 1}, ""),
                          ({"kind": "save_advantage", "magical": True, "mode": "normal"}, "")):
            e, _w = run(with_trait(eff))
            self.assertTrue([m for m in e if "save_advantage" in m or "/effect" in m], (eff, e))

    def test_duplicate_warns_distinct_stack(self):
        mr = {"kind": "save_advantage", "magical": True}
        _e, w = run(with_trait(mr, copy.deepcopy(mr)))
        self.assertTrue([m for m in w if "duplicate save_advantage" in m], w)
        _e, w = run(with_trait(mr, {"kind": "save_advantage", "save_vs_conditions": ["charmed"]}))
        self.assertFalse([m for m in w if "duplicate" in m], w)

    def test_not_at4(self):
        d = with_trait({"kind": "save_advantage", "magical": True})
        e, w = h.validate_in_memory(dict(d, **{"$schema": "monster@4"}), "monsters/b3.json")
        self.assertTrue(e or [m for m in w if "save_advantage" in m])


def bite(**keys) -> dict:
    d = bugbear()
    d["actions"][0].update(keys)
    return d


class Drain(unittest.TestCase):
    def test_valid(self):
        for dr in ({"max_hp": True, "save": {"ability": "con", "dc": 13}}, {"max_hp": True}):
            e, _w = run(bite(drain=dr))
            self.assertEqual(e, [], dr)

    def test_refused(self):
        for dr in ({"max_hp": False}, {}, {"max_hp": True, "save": {"ability": "wis", "dc": 13}},
                   {"max_hp": True, "save": {"ability": "con"}}, {"max_hp": True, "heal": True}):
            e, _w = run(bite(drain=dr))
            self.assertTrue([m for m in e if "drain" in m], (dr, e))

    def test_weapon_only(self):
        d = bugbear()
        d["actions"].append({"id": "x", "name": "X", "kind": "special", "description": "x", "drain": {"max_hp": True}})
        e, _w = run(d)
        self.assertTrue([m for m in e if "read only on weapon attacks" in m], e)


def cured(cond: str, **extra) -> dict:
    x = {"condition": cond, "duration": {"kind": "until_cured"}, **extra}
    return bite(on_hit=[{"kind": "save_condition", "save": {"ability": "con", "dc": 12}, "apply": [x]}])


class UntilCured(unittest.TestCase):
    def test_valid(self):
        for c in ("poisoned", "blinded", "deafened", "paralyzed"):
            e, _w = run(cured(c))
            self.assertEqual(e, [], c)

    def test_refused(self):
        for c in ("prone", "frightened", "grappled"):
            e, _w = run(cured(c))
            self.assertTrue([m for m in e if "until_cured needs" in m], (c, e))
        e, _w = run(cured("poisoned", repeat_save="end_of_turn"))
        self.assertTrue([m for m in e if "until_cured takes no repeat_save" in m], e)

    def test_outside_monster_apply(self):
        d = bugbear()
        d["traits"].append({"id": "t", "name": "T", "description": "t",
                            "effect": {"kind": "x", "duration": {"kind": "until_cured"}}})
        e, _w = run(d)
        self.assertTrue([m for m in e if "until_cured is read only" in m or "/effect" in m], e)


class Curse(unittest.TestCase):
    OK = {"no_regain_hp": True, "save": {"ability": "con", "dc": 12}}

    def test_valid(self):
        e, _w = run(bite(curse=self.OK))
        self.assertEqual(e, [])

    def test_refused(self):
        for cu in ({"no_regain_hp": True}, {"no_regain_hp": False, "save": {"ability": "con", "dc": 12}},
                   {"no_regain_hp": True, "save": {"ability": "wis", "dc": 12}}, dict(self.OK, max_hp_loss="3d6")):
            e, _w = run(bite(curse=cu))
            self.assertTrue([m for m in e if "curse" in m], (cu, e))

    def test_weapon_only(self):
        d = bugbear()
        d["actions"].append({"id": "x", "name": "X", "kind": "special", "description": "x", "curse": self.OK})
        e, _w = run(d)
        self.assertTrue([m for m in e if "read only on weapon attacks" in m], e)


class StrengthDrain(unittest.TestCase):
    OK = {"ability": "str", "dice": "1d4"}

    def test_valid(self):
        e, _w = run(bite(ability_drain=self.OK))
        self.assertEqual(e, [])

    def test_refused(self):
        for ad in ({"ability": "dex", "dice": "1d4"}, {"ability": "str"}, {"dice": "1d4"}, dict(self.OK, until="rest"),
                   {"ability": "str", "dice": "d4"}):
            e, _w = run(bite(ability_drain=ad))
            self.assertTrue(e, ad)

    def test_weapon_only(self):
        d = bugbear()
        d["actions"].append({"id": "x", "name": "X", "kind": "special", "description": "x", "ability_drain": self.OK})
        e, _w = run(d)
        self.assertTrue([m for m in e if "read only on weapon attacks" in m], e)


class AttackAbility(unittest.TestCase):
    """CONTENT_CONVENTIONS "Attack ability": optional monster@5 action `ability` (str|dex|none); absent stays valid."""

    def test_valid(self):
        for v in ("str", "dex", "none"):
            e, _w = run(bite(ability=v))
            self.assertEqual(e, [], v)
        e, _w = run(bite())
        self.assertEqual(e, [])

    def test_refused(self):
        for v in ("con", "STR", 1, None):
            e, _w = run(bite(ability=v))
            self.assertTrue(e, v)

    def test_weapon_only(self):
        d = bugbear()
        d["actions"].append({"id": "x", "name": "X", "kind": "special", "description": "x", "ability": "str"})
        e, _w = run(d)
        self.assertTrue([m for m in e if "read only on weapon attacks" in m], e)


class VampireDrain(unittest.TestCase):
    GATE = ["willing", "grappled_by_self", "incapacitated", "restrained"]

    def bite(self, **keys):
        d = bugbear()
        d["actions"][0]["damage"].append({"dice": "2d6", "type": "necrotic"})
        d["actions"][0].update(keys)
        return d

    def test_valid(self):
        e, _w = run(self.bite(drain={"max_hp": True, "damage_type": "necrotic", "heal_self": True}, target_requires_any=self.GATE))
        self.assertEqual(e, [])

    def test_refused(self):
        e, _w = run(self.bite(drain={"max_hp": True, "damage_type": "fire"}))
        self.assertTrue([m for m in e if "drain.damage_type is not one of" in m], e)
        for g in ([], ["asleep"], ["willing", "willing"]):
            e, _w = run(self.bite(target_requires_any=g))
            self.assertTrue([m for m in e if "target_requires_any" in m], (g, e))
        d = bugbear()
        d["actions"].append({"id": "x", "name": "X", "kind": "special", "description": "x", "target_requires_any": ["willing"]})
        e, _w = run(d)
        self.assertTrue([m for m in e if "read only on weapon attacks" in m], e)


class MultiattackCount(unittest.TestCase):
    def fungus(self, schema="monster@5", attacks=("morningstar",), count="1d4"):
        d = bugbear()
        d["$schema"] = schema
        d["traits"].append({"id": "multiattack", "name": "Multiattack", "description": "x", "effect": {
            "kind": "multiattack", "options": [{"id": "touches", "attacks": list(attacks), "count_dice": count}]}})
        return d

    def test_valid(self):
        e, _w = run(self.fungus())
        self.assertEqual(e, [])

    def test_refused(self):
        e, _w = run(self.fungus(attacks=("morningstar", "morningstar")))
        self.assertTrue([m for m in e if "count_dice needs exactly one attack" in m], e)
        e, _w = run(self.fungus(count="1d4+1"))
        self.assertTrue(e, e)


class TeleportAction(unittest.TestCase):
    def dog(self, schema="monster@5", **keys):
        d = bugbear()
        d["$schema"] = schema
        act = {"id": "teleport", "name": "Teleport", "kind": "teleport", "description": "x", "feet": 40,
               "usage": {"kind": "recharge", "min": 4}, "with_attack": "morningstar"}
        act.update(keys)
        d["actions"].append(act)
        return d

    def test_valid(self):
        e, _w = run(self.dog())
        self.assertEqual(e, [])

    def test_refused(self):
        e, _w = run(self.dog(with_attack="nope"))
        self.assertTrue([m for m in e if "with_attack" in m], e)
        for bad in ({"feet": 42}, {"feet": 0}):
            e, _w = run(self.dog(**bad))
            self.assertTrue(e, bad)
        d = self.dog()
        del d["actions"][-1]["feet"]
        e, _w = run(d)
        self.assertTrue(e)
        d = bugbear()
        d["actions"][0]["feet"] = 40
        e, _w = run(d)
        self.assertTrue(e)


STENCH = {"kind": "aura", "radius_ft": 5, "on": "start_turn", "save": {"ability": "con", "dc": 10},
          "condition": "poisoned", "duration": "until_start_of_next_turn", "success_immunity_hours": 24}


class Aura(unittest.TestCase):
    """Ghast Stench (SRD 5.1): "Any creature that starts its turn within 5 feet of the ghast must succeed on a DC 10
    Constitution saving throw or be poisoned until the start of its next turn. On a successful saving throw, the creature
    is immune to the ghast's Stench for 24 hours."""

    def test_valid(self):
        e, w = run(with_trait(copy.deepcopy(STENCH)))
        self.assertEqual(e, [])
        self.assertFalse([m for m in w if "aura" in m], w)
        no_imm = copy.deepcopy(STENCH)
        del no_imm["success_immunity_hours"]
        e, _w = run(with_trait(no_imm))
        self.assertEqual(e, [])

    def test_refused(self):
        for patch in ({"radius_ft": 7}, {"radius_ft": 0}, {"on": "end_turn"}, {"save": {"ability": "con"}},
                      {"save": {"ability": "luck", "dc": 10}}, {"save": {"ability": "con", "dc": 10, "x": 1}},
                      {"condition": "exhaustion"}, {"duration": "1_minute"}, {"success_immunity_hours": 0}, {"note": 1}):
            eff = copy.deepcopy(STENCH)
            eff.update(patch)
            e, _w = run(with_trait(eff))
            self.assertTrue(e, patch)

    def test_needs_at5(self):
        e, _w = run(with_trait(copy.deepcopy(STENCH), schema="monster@4"))
        self.assertTrue(e, e)


class AreaEffect(unittest.TestCase):
    """monster@5 area_effect (AOE_MONSTER_ABILITIES PR2): cone, line, cube, sphere, recharge, save."""

    def breath(self, schema="monster@5", **keys):
        d = bugbear()
        d["$schema"] = schema
        act = {"id": "breath", "name": "Breath", "kind": "area_effect", "description": "x",
               "save": {"ability": "dex", "dc": 14},
               "area": {"shape": "cone", "size_ft": 30}, "origin": "self",
               "damage": [{"dice": "6d6", "type": "fire"}], "on_success": "half",
               "usage": {"kind": "recharge", "min": 5}}
        act.update(keys)
        d["actions"].append(act)
        return d

    def test_shapes_and_recharge(self):
        for shape, area in (("cone", {"shape": "cone", "size_ft": 30}),
                            ("line", {"shape": "line", "size_ft": 30, "width_ft": 5}),
                            ("cube", {"shape": "cube", "size_ft": 15}),
                            ("sphere", {"shape": "sphere", "size_ft": 5}),
                            ("cylinder", {"shape": "cylinder", "size_ft": 10, "height_ft": 20})):
            e, _w = run(self.breath(area=area, usage={"kind": "recharge", "min": 6}))
            self.assertEqual(e, [], shape)
        e, _w = run(self.breath(damage=[{"dice": "2d4", "type": "cold"}], on_success="half",
                                usage={"kind": "recharge", "min": 6}, area={"shape": "cone", "size_ft": 15}))
        self.assertEqual(e, [])
        blind = self.breath()
        del blind["actions"][-1]["damage"]
        del blind["actions"][-1]["on_success"]
        blind["actions"][-1]["apply"] = [{"condition": "blinded", "duration": {"kind": "rounds", "n": 10},
                                          "repeat_save": "end_of_turn"}]
        blind["actions"][-1]["usage"] = {"kind": "recharge", "min": 6}
        e, _w = run(blind)
        self.assertEqual(e, [], e)

    def test_refused(self):
        e, _w = run(self.breath(area={"shape": "cone", "size_ft": 15, "width_ft": 5}))
        self.assertTrue(e, e)
        line = self.breath(area={"shape": "line", "size_ft": 30})
        e, _w = run(line)
        self.assertTrue(e, e)
        e, _w = run(self.breath(usage={"kind": "recharge", "min": 1}))
        self.assertTrue(e, e)
        d = self.breath()
        d["actions"][-1]["range_ft"] = 30
        e, _w = run(d)
        self.assertTrue([m for m in e if "range_ft" in m], e)
        d = self.breath()
        d["actions"].append(dict(d["actions"][-1]))
        e, _w = run(d)
        self.assertTrue([m for m in e if "already used" in m], e)
        e, _w = run(self.breath(schema="monster@4"))
        self.assertTrue(e, e)
        d = self.breath()
        d["actions"][-1]["apply"] = [{"condition": "stunned", "duration": {"kind": "concentration"}}]
        _e, w = run(d)
        self.assertTrue([m for m in w if "area_effect: unsupported usage or condition duration" in m], w)


if __name__ == "__main__":
    unittest.main()
