#!/usr/bin/env python3
"""Key stats of the srd51.core monster@4 batch vs the 5e-database table (ctest: content_validate_srd51_monsters).

`data/srd51_monsters_5e_database_a6212be.json` holds fields copied verbatim from 5e-bits/5e-database commit
a6212be (src/2014/en/5e-SRD-Monsters.json). For every authored monster this asserts AC, HP (average and
formula), CR / XP, size, creature type, speed, abilities, defenses and every attack (to-hit and damage dice).
Deviations from the table are listed in DEVIATIONS below with the reason.

Also pins the authored/deferred split (docs/SRD_MONSTERS_DEFERRED.md) and the mechanics that are data, not text.
"""
from __future__ import annotations

import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACK = ROOT / "content/packs/srd51.core"
TABLE = json.loads((Path(__file__).parent / "data/srd51_monsters_5e_database_a6212be.json").read_text(encoding="utf-8"))["monsters"]

CR = {"0": 0, "1/8": 0.125, "1/4": 0.25, "1/2": 0.5}
# Authored in this batch (monster@4, converted to monster@5 with derive: true). The goblin stays monster@3 (not part of this batch).
AUTHORED = sorted(TABLE)
# Deferred (docs/SRD_MONSTERS_DEFERRED.md "Deferred", 53 / 17): rat, scorpion (flat 1 damage not expressible) + tier B batch d1.
DEFERRED: list[str] = ["rat", "scorpion", "cat", "crab", "poisonous-snake", "quipper", "reef-shark", "giant-crab", "constrictor-snake",
                       "crocodile", "giant-frog", "elk", "giant-goat", "giant-sea-horse", "warhorse", "lion", "tiger"]
# Authored attacks with no table damage option: the table copies one damage line per attack, the SRD text has a second.
EXTRA_ATTACKS = {("bugbear", "javelin_thrown"): "SRD: 5 (1d6 + 2) piercing damage at range; the table lists only the melee 2d6 + 2."}
# Documented deviations: monster -> why the content differs from the raw table.
DEVIATIONS = {
    "lizardfolk": "AC: the table lists 13 (natural armor) and 15 (with shield); the SRD line is 15 and the Spiked Shield attack needs the shield.",
    "black-bear": "skills: the table omits Perception +3 although passive Perception is 13 (SRD block); the validator requires 10 + skill.",
    "gargoyle": "damage_resistances stays empty: the qualified resistance lives in the damage_mitigation trait effect.",
    "riding-horse": "xp: the table lists 25 XP; SRD 5.1 prints Challenge 1/4 (50 XP), and the SRD is authoritative (CR 1/4 = 50 XP).",
    "swarm-of-rats": "type: the table says 'swarm of Tiny beasts', not in the creature_type enum; convention is beast + tag swarm (T&C-G15, sim #172).",
}


def load(idx):
    return json.loads((PACK / "monsters" / f"{idx.replace('-', '_')}.json").read_text(encoding="utf-8"))


def num(s):
    return int(str(s).split()[0])


class MonsterTable(unittest.TestCase):
    def test_every_monster_matches_the_table(self):
        for idx in AUTHORED:
            with self.subTest(monster=idx):
                t, d = TABLE[idx], load(idx)
                self.assertEqual(d["$schema"], "monster@5")   # EQUIPMENT 7.2: converted with derive on (PR B)
                self.assertEqual(d["id"], "srd51.monster." + idx.replace("-", "_"))
                self.assertEqual(d["name"], t["name"])
                self.assertEqual(d["size"], t["size"].lower())
                if idx == "swarm-of-rats":
                    self.assertEqual((d["type"], d["tags"]), ("beast", ["swarm"]))
                else:
                    self.assertEqual(d["type"], t["type"])
                self.assertIn(d["armor_class"]["value"], t["ac"])
                if idx != "lizardfolk":
                    self.assertEqual(d["armor_class"]["value"], t["ac"][0])
                self.assertEqual(d["hit_points"], {"average": t["hp"], "formula": t["hp_roll"]})
                self.assertEqual(d["challenge"]["xp"], 50 if idx == "riding-horse" else t["xp"])
                rating = d["challenge"]["rating"]
                self.assertEqual(CR[rating] if rating in CR else int(rating), t["cr"])
                speed = {k: num(v) for k, v in t["speed"].items()}
                speed.setdefault("walk", 0)   # the table has no walk key for swim-only creatures (reef shark: swim 40 ft.)
                self.assertEqual(speed, d["speed"])
                self.assertEqual([d["abilities"][k] for k in ("str", "dex", "con", "int", "wis", "cha")], t["abilities"])
                self.assertEqual(d.get("damage_vulnerabilities", []), t["damage_vulnerabilities"])
                self.assertEqual(d.get("damage_immunities", []), t["damage_immunities"])
                if idx != "gargoyle":
                    self.assertEqual(d.get("damage_resistances", []), t["damage_resistances"])
                self.assertEqual(d.get("condition_immunities", []), t["condition_immunities"])
                self.assertEqual(d["proficiency_bonus"], 2 if t["cr"] <= 4 else 3)   # SRD: CR 0-4 +2, CR 5-8 +3 (medusa, CR 6)

    def test_every_attack_matches_the_table(self):
        for idx in AUTHORED:
            t, d = TABLE[idx], load(idx)
            attacks = [a for a in d["actions"] if a["kind"] in ("melee_weapon_attack", "ranged_weapon_attack")]
            for ta in t["attacks"]:
                for opt in ta["damage_options"]:
                    with self.subTest(monster=idx, attack=ta["name"], damage=opt):
                        want = [{"dice": x, "type": y} for x, y in opt]
                        self.assertTrue(
                            any(a["to_hit"] == ta["attack_bonus"] and a["damage"] == want for a in attacks),
                            f"no action with to_hit {ta['attack_bonus']} and damage {want}")
            # no invented attack: every authored attack action matches some table option
            known = [(ta["attack_bonus"], [{"dice": x, "type": y} for x, y in opt]) for ta in t["attacks"] for opt in ta["damage_options"]]
            for a in attacks:
                if (idx, a["id"]) in EXTRA_ATTACKS:
                    continue
                with self.subTest(monster=idx, action=a["id"]):
                    self.assertIn((a["to_hit"], a["damage"]), known)

    def test_attack_shapes(self):
        for idx in AUTHORED:
            for a in load(idx)["actions"]:
                with self.subTest(monster=idx, action=a["id"]):
                    if a["kind"] == "melee_weapon_attack":
                        self.assertIn("reach", a)
                    if a["kind"] == "ranged_weapon_attack":
                        n, lng = a["range"]
                        self.assertLess(n, lng)

    def test_multiattack_names_real_attack_actions(self):
        for idx in AUTHORED:
            d = load(idx)
            ids = {a["id"] for a in d["actions"] if a["kind"] in ("melee_weapon_attack", "ranged_weapon_attack")}
            for tr in d.get("traits", []):
                e = tr.get("effect", {})
                if e.get("kind") == "multiattack":
                    for opt in e["options"]:
                        with self.subTest(monster=idx, option=opt["id"]):
                            self.assertTrue(set(opt["attacks"]) <= ids)

    def test_multiattack_shapes(self):
        def opts(idx):
            return {o["id"]: o["attacks"] for t in load(idx)["traits"] if t.get("effect", {}).get("kind") == "multiattack"
                    for o in t["effect"]["options"]}
        self.assertEqual(opts("black-bear"), {"bite_claws": ["bite", "claws"]})
        self.assertEqual(opts("gargoyle"), {"bite_claws": ["bite", "claws"]})
        self.assertEqual(opts("animated-armor"), {"slam_slam": ["slam", "slam"]})
        self.assertEqual(opts("scout"), {"melee": ["shortsword"] * 2, "ranged": ["longbow"] * 2})
        self.assertEqual(opts("spy"), {"shortsword_x2": ["shortsword"] * 2})
        lf = opts("lizardfolk")
        self.assertEqual(len(lf), 6)   # every unordered pair of the four melee weapons ("each one with a different weapon")
        self.assertTrue(all(len(set(v)) == 2 for v in lf.values()))

    def test_mechanics_are_data(self):
        z = {t["id"]: t["effect"] for t in load("zombie")["traits"] if "effect" in t}["undead_fortitude"]
        self.assertEqual((z["kind"], z["on"]), ("on_event", "drop_to_zero"))
        self.assertIn({"kind": "save", "ability": "con", "dc_from_damage": True, "base": 5}, z["when"])
        self.assertEqual(z["effects"], [{"kind": "set_hp", "value": 1}])
        garg = {t["id"]: t["effect"] for t in load("gargoyle")["traits"] if "effect" in t}["damage_resistances_nonmagical"]
        self.assertEqual(garg["resistances"], {"nonmagical_not_adamantine": ["bludgeoning", "piercing", "slashing"]})
        breath = [a for a in load("black-dragon-wyrmling")["actions"] if a["kind"] == "save_effect"][0]
        self.assertEqual(breath["save"], {"ability": "dex", "dc": 11})
        self.assertEqual(breath["damage"], [{"dice": "5d8", "type": "acid"}])
        self.assertEqual((breath["on_success"], breath["usage"]), ("half", {"kind": "recharge", "min": 5}))
        self.assertEqual(breath["area"], {"shape": "line", "size_ft": 15, "width_ft": 5})
        bite = load("giant-poisonous-snake")["actions"][0]
        self.assertEqual(bite["reach"], 10)
        self.assertEqual(bite["on_hit"][0]["damage"], [{"dice": "3d6", "type": "poison"}])
        self.assertEqual((bite["on_hit"][0]["save"], bite["on_hit"][0]["on_success"]), ({"ability": "con", "dc": 11}, "half"))
        self.assertEqual(load("skeleton")["damage_vulnerabilities"], ["bludgeoning"])
        # Pack Tactics (common@1 open kind pack_tactics, sim #127): one declaration, no fields
        for idx in ("jackal", "kobold", "wolf", "dire-wolf", "hyena", "tribal-warrior"):
            with self.subTest(monster=idx):
                effs = [t["effect"] for t in load(idx)["traits"] if "effect" in t]
                self.assertEqual(effs, [{"kind": "pack_tactics"}])
        # bite knock-prone rider (sim #133): on_hit save_condition, Str DC 11 / 13, prone untimed (ends when the target
        # stands up; applied untracked like Shove)
        for idx, dc in (("wolf", 11), ("dire-wolf", 13), ("mastiff", 11)):
            with self.subTest(monster=idx):
                bite = load(idx)["actions"][0]
                self.assertEqual(bite["on_hit"], [{"kind": "save_condition", "save": {"ability": "str", "dc": dc},
                                                   "apply": [{"condition": "prone"}]}])
                self.assertIn("knocked prone", bite["description"])
        # Aggressive / Reckless (common@1 open kinds aggressive / reckless_attack, sim #129): one declaration, no fields
        for idx, kind in (("orc", "aggressive"), ("berserker", "reckless_attack")):
            with self.subTest(monster=idx):
                self.assertEqual([t["effect"] for t in load(idx)["traits"] if "effect" in t], [{"kind": kind}])
        # sim #158 predicates / on_event usage
        eff = lambda idx, tid: {t["id"]: t.get("effect") for t in load(idx)["traits"]}[tid]
        sa = eff("bugbear", "surprise_attack")
        self.assertEqual((sa["on"], sa["when"]), ("hit", [{"kind": "target_surprised"}]))
        self.assertEqual(sa["effects"], [{"kind": "extra_damage", "damage": [{"dice": "2d6", "type": "same_as_weapon"}]}])
        self.assertEqual(eff("bugbear", "brute"), {"kind": "weapon_extra_dice", "dice": 1, "melee_only": True})   # read only by derive; the printed dice stay baked
        ma = eff("hobgoblin", "martial_advantage")
        self.assertEqual((ma["on"], ma["limit"]), ("hit", "once_per_turn"))
        self.assertEqual(ma["when"], [{"kind": "weapon_attack"}, {"kind": "ally_near_target"}])
        rl = eff("boar", "relentless")
        self.assertEqual((rl["on"], rl["when"]), ("drop_to_zero", [{"kind": "damage_at_most", "amount": 7}]))
        self.assertEqual((rl["usage"], rl["effects"]), ({"kind": "per_rest", "uses": 1, "rest": "short"}, [{"kind": "set_hp", "value": 1}]))
        ch = eff("boar", "charge")
        self.assertIn({"kind": "moved_at_least_ft", "ft": 20}, ch["when"])
        self.assertEqual(ch["effects"][1], {"kind": "save_condition", "save": {"ability": "str", "dc": 11}, "apply": [{"condition": "prone"}]})
        # sim #170: ghoul claws (rider when[]: undead and elves exempt; a half-elf race has no "elf" tag), giant spider Web
        claws = {a["id"]: a for a in load("ghoul")["actions"]}["claws"]["on_hit"]
        self.assertEqual(claws, [{"kind": "save_condition", "save": {"ability": "con", "dc": 10},
                                  "apply": [{"condition": "paralyzed", "duration": {"kind": "rounds", "n": 10}, "repeat_save": "end_of_turn"}],
                                  "when": [{"kind": "not_creature_type", "creature_types": ["undead"]}, {"kind": "not_target_tag", "tag": "elf"}]}])
        web = {a["id"]: a for a in load("giant-spider")["actions"]}["web"]
        self.assertEqual((web["kind"], web["attack"], web["to_hit"], web["range_ft"], web["long_range_ft"]),
                         ("attack_effect", "ranged", 5, 30, 60))   # sim #174: Web 30/60
        # sim #174: the webbing is a destructible object (AC 10; hp 5; vulnerable fire; immune bludgeoning, poison, psychic)
        self.assertEqual(web["apply"], [{"condition": "restrained", "escape": {"ability": "str", "dc": 12},
                                         "object": {"ac": 10, "hp": 5, "vulnerable": ["fire"],
                                                    "immune": ["bludgeoning", "poison", "psychic"]}}])
        self.assertEqual(web["usage"], {"kind": "recharge", "min": 5})
        sbite = {a["id"]: a for a in load("giant-spider")["actions"]}["bite"]["on_hit"]
        self.assertEqual(sbite, [{"kind": "save_condition", "save": {"ability": "con", "dc": 11},
                                  "damage": [{"dice": "2d8", "type": "poison"}], "on_success": "half",
                                  # sim #174: 0 HP from the poison -> stable, poisoned 1 hour (600 rounds), paralyzed while poisoned
                                  "zero_hp": {"condition": "poisoned", "while": "paralyzed", "duration": {"kind": "rounds", "n": 600}}}])
        # sim #172: swarm of rats: tag swarm (shared spaces, no healing / temp HP), bites 2d6 or 1d6 at half HP or fewer
        rats = load("swarm-of-rats")
        self.assertEqual(rats["tags"], ["swarm"])
        bites = rats["actions"][0]
        self.assertEqual((bites["id"], bites["reach"], bites["damage"], bites["half_hp_damage"]),
                         ("bites", 0, [{"dice": "2d6", "type": "piercing"}], "1d6"))
        self.assertNotIn("effect", {t["id"]: t for t in rats["traits"]}["swarm"])   # the tag carries the trait
        # Petrifying Gaze (common@2 gaze arm, 7.2p, sim #153): basilisk DC 12 Con, medusa DC 14 Con with the 5-point instant
        # petrification margin; both restrained, repeat save at the end of the turn, petrified on a failed repeat save.
        for idx, dc, margin in (("basilisk", 12, 0), ("medusa", 14, 5)):
            with self.subTest(monster=idx):
                g = {t["id"]: t for t in load(idx)["traits"]}["petrifying_gaze"]
                self.assertEqual(g["effect"], {"kind": "gaze", "range_ft": 30, "save": {"ability": "con", "dc": dc},
                                               "condition": "restrained", "repeat_save": "end_of_turn", "escalates": True,
                                               "escalate_to": "petrified", "escalate_margin": margin})
                self.assertIn(f"DC {dc} Constitution saving throw", g["description"])
        med = {t["id"]: t for t in load("medusa")["traits"]}["multiattack"]["effect"]
        self.assertEqual(med["options"], [{"id": "melee", "attacks": ["snake_hair", "shortsword", "shortsword"]},
                                          {"id": "ranged", "attacks": ["longbow", "longbow"]}])
        # Noble Parry (reactions[], R-G7): +2 AC against one melee attack it sees
        self.assertEqual(load("noble")["reactions"][0]["window"], {"kind": "being_hit", "melee_only": True, "requires_sight": True})
        self.assertEqual(load("noble")["reactions"][0]["effect"], {"kind": "ac_bonus", "bonus": 2})
        # Cultist Dark Devotion: partial, description only (no condition-keyed save advantage construct)
        self.assertNotIn("effect", {t["id"]: t for t in load("cultist")["traits"]}["dark_devotion"])
        # Sunlight Sensitivity / Keen Hearing and Smell: descriptive (no light model, no perception checks in the sim)
        self.assertNotIn("effect", {t["id"]: t for t in load("kobold")["traits"]}["sunlight_sensitivity"])

    def test_engulf_swallow_transparent(self):
        # Rules 7.2w (#189): Engulf (save_effect + contain engulf, Str DC 12 escape, Large or smaller), Transparent DC 15
        cube = load("gelatinous-cube")
        en = {a["id"]: a for a in cube["actions"]}["engulf"]
        self.assertEqual((en["kind"], en["save"], en["damage"], en["on_success"]),
                         ("save_effect", {"ability": "dex", "dc": 12}, [{"dice": "3d6", "type": "acid"}], "none"))
        self.assertEqual(en["apply"], [{"condition": "restrained", "escape": {"ability": "str", "dc": 12}, "max_size": "large",
                                        "contain": {"kind": "engulf", "damage": {"dice": "6d6", "type": "acid"}}}])
        self.assertEqual({t["id"]: t.get("effect") for t in cube["traits"]},
                         {"ooze_cube": None, "transparent": {"kind": "transparent", "dc": 15}})
        # Swallow (attack_effect + contain swallow: blinded, total cover, no escape; Medium or smaller); bite grapples (escape DC 13)
        toad = {a["id"]: a for a in load("giant-toad")["actions"]}
        self.assertEqual(toad["bite"]["on_hit"], [{"kind": "grapple", "escape_dc": 13, "restrains": True, "exclusive": True}])
        sw = toad["swallow"]
        self.assertEqual((sw["kind"], sw["attack"], sw["to_hit"], sw["range_ft"]), ("attack_effect", "melee", 4, 5))
        self.assertEqual(sw["apply"], [{"condition": "restrained", "max_size": "medium",
                                        "contain": {"kind": "swallow", "damage": {"dice": "3d6", "type": "acid"}}}])

    def test_authored_and_deferred_split(self):
        have = sorted(p.stem for p in (PACK / "monsters").glob("*.json"))
        self.assertEqual(have, sorted([i.replace("-", "_") for i in AUTHORED] + ["goblin"]))
        for idx in DEFERRED:
            self.assertFalse((PACK / "monsters" / f"{idx.replace('-', '_')}.json").exists(), idx)
        doc = (ROOT / "docs/SRD_MONSTERS_DEFERRED.md").read_text(encoding="utf-8")
        for idx in DEFERRED:
            self.assertIn("srd51.monster." + idx.replace("-", "_"), doc)
        man = json.loads((PACK / "manifest.json").read_text(encoding="utf-8"))
        for p in have:
            self.assertIn(f"monsters/{p}.json", man["contents"])
        self.assertEqual(man["api_level"], 2)

    def test_goblin_untouched(self):
        g = json.loads((PACK / "monsters/goblin.json").read_text(encoding="utf-8"))
        self.assertEqual((g["$schema"], g["id"]), ("monster@3", "srd51.monster.goblin"))


if __name__ == "__main__":
    unittest.main()
