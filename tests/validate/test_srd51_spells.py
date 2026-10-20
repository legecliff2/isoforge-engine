#!/usr/bin/env python3
"""srd51.core spell@1 batch (spells refresh) vs the 5e-database table (ctest: content_validate_srd51_spells).

`data/srd51_spells_5e_database_a6212be.json` holds fields copied from 5e-bits/5e-database commit a6212be
(src/2014/en/5e-SRD-Spells.json). For every spell in the table this asserts level, school, classes, casting time, range,
components, duration / concentration, damage dice and type at the base level, upcast / cantrip scaling, the save, the
attack type and the area. The mechanics the loaders read (forced_move push, teleport, timed modifiers) are pinned in
test_mechanics. docs/SRD_SPELLS_DEFERRED.md lists the authored / deferred split.
"""
from __future__ import annotations

import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACK = ROOT / "content/packs/srd51.core"
TABLE = json.loads((Path(__file__).parent / "data/srd51_spells_5e_database_a6212be.json").read_text(encoding="utf-8"))["spells"]
CAST = {"1 action": "action", "1 bonus action": "bonus_action", "1 reaction": "reaction"}


def load(idx):
    return json.loads((PACK / "spells" / f"{idx.replace('-', '_')}.json").read_text(encoding="utf-8"))


def dice_count(d):
    return int(re.match(r"(\d+)d", d).group(1))


class SpellTable(unittest.TestCase):
    def test_every_spell_matches_the_table(self):
        for idx, t in TABLE.items():
            with self.subTest(spell=idx):
                d = load(idx)
                self.assertEqual(d["$schema"], "spell@1")
                self.assertEqual(d["id"], "srd51.spell." + idx.replace("-", "_"))
                self.assertEqual((d["name"], d["level"], d["school"]), (t["name"], t["level"], t["school"]))
                self.assertEqual(d["classes"], t["classes"])
                self.assertEqual(d["casting_time"]["kind"], CAST[t["casting_time"]])
                rng = d["range"]
                if t["range"] in ("Self", "Touch"):
                    self.assertEqual(rng, {"kind": t["range"].lower()})
                else:
                    self.assertEqual(rng, {"kind": "ranged", "feet": int(t["range"].split()[0])})
                comp = {k.upper() for k in ("v", "s", "m") if k in d["components"]}
                self.assertEqual(comp, set(t["components"]))
                if "material" in t:
                    self.assertEqual(d["components"]["m"]["text"].lower(), t["material"].rstrip(".").lower())
                du = d["duration"]
                self.assertEqual(du.get("concentration", False), t["concentration"])
                if t["duration"] == "Instantaneous":
                    self.assertEqual(du, {"kind": "instantaneous"})
                else:
                    n, unit = t["duration"].replace("Up to ", "").split()
                    self.assertEqual((du["kind"], du["amount"]), (unit.rstrip("s") + "s", int(n)))
                eff = d["effects"][0]
                if "damage_type" in t:
                    lv = t["damage_at"]
                    base = lv[str(min(int(k) for k in lv))]
                    self.assertEqual(eff["damage"], {"dice": base, "type": t["damage_type"]})
                    if d["level"] == 0:
                        self.assertEqual(d["scaling"]["cantrip"], {"levels": [int(k) for k in sorted(lv, key=int)][1:], "adds": "dice"})
                    else:
                        step = dice_count(lv[str(d["level"] + 1)]) - dice_count(base)
                        self.assertEqual(d["scaling"]["upcast"], {"dice": step})
                if "dc" in t:
                    self.assertEqual((eff["kind"], eff["ability"], eff["on_success"]), ("save", t["dc"]["ability"], t["dc"]["success"]))
                if "attack_type" in t:
                    self.assertEqual((eff["kind"], eff["attack"]), ("spell_attack", t["attack_type"]))
                if "area_of_effect" in t:
                    a = t["area_of_effect"]
                    self.assertEqual((d["area"]["shape"], d["area"]["size_ft"]), (a["type"], a["size"]))
                else:
                    self.assertNotIn("area", d)

    def test_manifest(self):
        man = json.loads((PACK / "manifest.json").read_text(encoding="utf-8"))
        for idx in TABLE:
            self.assertIn(f"spells/{idx.replace('-', '_')}.json", man["contents"])

    def test_mechanics(self):
        # Thunderwave: push 10 ft, failed saves only (R-G9 forced_move rider)
        self.assertEqual(load("thunderwave")["effects"][1], {"kind": "forced_move", "applies_to": "affected", "mode": "push", "feet": 10})
        self.assertEqual(load("lightning-bolt")["area"], {"shape": "line", "size_ft": 100, "width_ft": 5})
        self.assertEqual(load("misty-step")["effects"], [{"kind": "teleport", "max_feet": 30}])
        # Ray of Frost: -10 ft speed until the start of the caster's next turn, only on a hit
        rf = load("ray-of-frost")["effects"][1]
        self.assertEqual(rf["applies_to"], "affected")
        self.assertEqual(rf["modifier"], {"stat": "speed", "speed_scale": {"num": 1, "den": 1}, "speed_sub": 10, "applies_to": "target",
                                          "duration": {"kind": "until_start_of_caster_turn"}})
        self.assertEqual(load("shield-of-faith")["effects"][0]["modifier"],
                         {"stat": "ac", "bonus": 2, "applies_to": "target", "duration": {"kind": "concentration"}})
        self.assertEqual(load("barkskin")["effects"][0]["modifier"],
                         {"stat": "ac", "ac_floor": 16, "applies_to": "target", "duration": {"kind": "concentration"}})


if __name__ == "__main__":
    unittest.main()
