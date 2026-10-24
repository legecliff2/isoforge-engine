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


if __name__ == "__main__":
    unittest.main()
