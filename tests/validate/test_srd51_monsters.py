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
# Deferred (docs/SRD_MONSTERS_DEFERRED.md "Deferred", 122 authored / 8 deferred): batch 3 and tier-A triage (5) + tier B still open (3: crocodile, lion, tiger). Quipper left the deferred list (Blood Frenzy description-only, swim-only walk 0). Giant Crab left once max_grapples landed (Druid gap G). Constrictor Snake and Elk left in CR 0.25 batch B. Giant Frog left in batch A (Swallow pattern); it stays authored and is not restored from batch B's stale deferred row. Giant Goat, Giant Sea Horse and Warhorse left in CR 1/2 batch D. Lion and tiger stay deferred (bonus-bite gap, as panther's note). CR 1/2 batch E (ice mephit, dust mephit, magma mephit, cockatrice, sahuagin) is authored; Cockatrice petrify and Sahuagin Blood Frenzy stay partial on those rows, not new deferred monsters.
DEFERRED: list[str] = ["doppelganger", "duergar", "green-hag", "mimic", "rust-monster", "crocodile", "lion", "tiger"]   # docs/SRD_MONSTERS_DEFERRED.md: 122 / 8
# Authored attacks with no table damage option: the table copies one damage line per attack, the SRD text has a second.
EXTRA_ATTACKS = {("bugbear", "javelin_thrown"): "SRD: 5 (1d6 + 2) piercing damage at range; the table lists only the melee 2d6 + 2."}
# Documented deviations: monster -> why the content differs from the raw table.
DEVIATIONS = {
    "lizardfolk": "AC: the table lists 13 (natural armor) and 15 (with shield); the SRD line is 15 and the Spiked Shield attack needs the shield.",
    "cult-fanatic": "HP: the a6212be mirror lists 22 (6d8-5), a mirror error; the SRD 5.1 block prints 33 (6d8 + 6), which Con 12 (+1 x 6 HD) gives. Authored as 33 (6d8+6) per Elf's ruling (HP_ERRATA).",
    "black-bear": "skills: the table omits Perception +3 although passive Perception is 13 (SRD block); the validator requires 10 + skill.",
    "gargoyle": "damage_resistances stays empty: the qualified resistance lives in the damage_mitigation trait effect.",
    "riding-horse": "xp: the table lists 25 XP; SRD 5.1 prints Challenge 1/4 (50 XP), and the SRD is authoritative (CR 1/4 = 50 XP).",
    "wight": "damage_resistances lists necrotic only: the qualified 'nonmagical weapons that aren't silvered' entry lives in the damage_mitigation trait effect (as gargoyle).",
    "vampire-spawn": "damage_resistances lists necrotic only: the qualified 'nonmagical weapons' entry lives in the damage_mitigation trait effect (as gargoyle).",
    "mummy": "damage_resistances is empty: the qualified 'nonmagical weapons' entry lives in the damage_mitigation trait effect (as gargoyle).",
    "shadow": "damage_resistances lists the five plain types: the qualified 'nonmagical weapons' entry lives in the damage_mitigation trait effect (as gargoyle).",
    "violet-fungus": "condition_immunities: the table lists blinded twice and omits deafened; the SRD line is blinded, deafened, frightened (as shrieker). Rotting Touch to_hit: the SRD prints +2 (the table agrees); the derivation gives -2 (Str 3 -4, PB +2). Carried as an approved attack_bonus override of +2 (printed value wins).",
    "shrieker": "condition_immunities: the table lists blinded twice and omits deafened; the SRD line is blinded, deafened, frightened.",
    "ghast": "Bite to_hit: the SRD prints +3 (the table agrees); the derivation gives +5 (Dex 17 +3, PB +2). Carried as an approved attack_bonus override of +3 (printed value wins).",
    "gladiator": "Spear (thrown) damage: the SRD prints one line for both uses, 11 (2d6 + 4); the derivation gives a thrown use no Brute die (1d6 + 4). Carried as an approved damage_override of 2d6+4 (CONTENT_CONVENTIONS 21; printed value wins).",
    "mule": "Hooves to_hit: the SRD prints +2 (the table agrees); the derivation gives +4 (Str 14 +2, PB +2). Carried as an approved attack_bonus override of +2 (printed value wins).",
    "swarm-of-rats": "type: the table says 'swarm of Tiny beasts', not in the creature_type enum; convention is beast + tag swarm (T&C-G15, sim #172).",
    "swarm-of-bats": "type: as swarm-of-rats (beast + tag swarm). Speed is walk 0 and fly 30, the printed line.",
    "swarm-of-ravens": "type: as swarm-of-rats (beast + tag swarm). skills: a6212be lists no proficiencies, but passive Perception is 15 and the SRD 5.1 block prints Perception +5 (Wis +1 and expertise). Authored as perception 5 (as black bear: the validator requires 10 + skill).",
    "panther": "One SRD 5.1 stat block. Leopard and Cougar are aliases of srd51.monster.panther, not separate documents.",
    "giant-wasp": "speed: a6212be lists walk 10 ft., fly 50 ft., and swim 50 ft. SRD 5.1 prints 10 ft., fly 50 ft. and no swim. Authored without swim (the SRD is authoritative, as riding-horse XP).",
    "swarm-of-insects": "type: as swarm-of-rats (beast + tag swarm). The base block is bites only (no poison). Variant insect swarms (beetle, centipede, spider, wasp) are an SRD sidebar and are not authored.",
}
# Mirror errors in hit points: monster -> (average, formula) as printed in the SRD 5.1 block (DEVIATIONS).
HP_ERRATA = {"cult-fanatic": (33, "6d8+6")}

QUALIFIED_RESISTANCES = ("gargoyle", "wight", "vampire-spawn", "shadow", "mummy")


def load(idx):
    return json.loads((PACK / "monsters" / f"{idx.replace('-', '_')}.json").read_text(encoding="utf-8"))


def dmg(x, y):
    """A bare integer in the table ('Hit: 1 piercing damage') is a flat component (CONTENT_CONVENTIONS 19)."""
    return {"flat": int(x), "type": y} if x.isdigit() else {"dice": x, "type": y}


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
                if idx in ("swarm-of-rats", "swarm-of-bats", "swarm-of-ravens", "swarm-of-insects"):
                    self.assertEqual((d["type"], d["tags"]), ("beast", ["swarm"]))
                else:
                    self.assertEqual(d["type"], t["type"])
                self.assertIn(d["armor_class"]["value"], t["ac"])
                if idx != "lizardfolk":
                    self.assertEqual(d["armor_class"]["value"], t["ac"][0])
                if idx in HP_ERRATA:
                    self.assertEqual((t["hp"], t["hp_roll"]), (22, "6d8-5"))   # the pinned mirror value stays verbatim
                    hp, roll = HP_ERRATA[idx]
                    self.assertEqual(d["hit_points"], {"average": hp, "formula": roll})
                else:
                    self.assertEqual(d["hit_points"], {"average": t["hp"], "formula": t["hp_roll"]})
                self.assertEqual(d["challenge"]["xp"], 50 if idx == "riding-horse" else t["xp"])
                rating = d["challenge"]["rating"]
                self.assertEqual(CR[rating] if rating in CR else int(rating), t["cr"])
                if idx == "giant-wasp":
                    # a6212be lists swim 50 ft.; SRD 5.1 prints walk 10 / fly 50 only (DEVIATIONS).
                    self.assertEqual(t["speed"], {"walk": "10 ft.", "fly": "50 ft.", "swim": "50 ft."})
                    self.assertEqual(d["speed"], {"walk": 10, "fly": 50})
                else:
                    speed = {k: num(v) for k, v in t["speed"].items()}
                    speed.setdefault("walk", 0)   # the table has no walk key for swim-only creatures (reef shark: swim 40 ft.)
                    self.assertEqual(speed, d["speed"])
                self.assertEqual([d["abilities"][k] for k in ("str", "dex", "con", "int", "wis", "cha")], t["abilities"])
                self.assertEqual(d.get("damage_vulnerabilities", []), t["damage_vulnerabilities"])
                self.assertEqual(d.get("damage_immunities", []), t["damage_immunities"])
                if idx in QUALIFIED_RESISTANCES:   # the qualified "nonmagical weapons" entry is a damage_mitigation trait (DEVIATIONS)
                    self.assertEqual(d.get("damage_resistances", []), [r for r in t["damage_resistances"] if "nonmagical" not in r])
                else:
                    self.assertEqual(d.get("damage_resistances", []), t["damage_resistances"])
                if idx in ("shrieker", "violet-fungus"):
                    self.assertEqual(d["condition_immunities"], ["blinded", "deafened", "frightened"])
                else:
                    self.assertEqual(d.get("condition_immunities", []), t["condition_immunities"])
                self.assertEqual(d["proficiency_bonus"], 2 if t["cr"] <= 4 else 3)   # SRD: CR 0-4 +2, CR 5-8 +3 (medusa, CR 6)

    def test_every_attack_matches_the_table(self):
        for idx in AUTHORED:
            t, d = TABLE[idx], load(idx)
            attacks = [a for a in d["actions"] if a["kind"] in ("melee_weapon_attack", "ranged_weapon_attack")]
            for ta in t["attacks"]:
                for opt in ta["damage_options"]:
                    with self.subTest(monster=idx, attack=ta["name"], damage=opt):
                        want = [dmg(x, y) for x, y in opt]
                        self.assertTrue(
                            any(a["to_hit"] == ta["attack_bonus"] and a["damage"] == want for a in attacks),
                            f"no action with to_hit {ta['attack_bonus']} and damage {want}")
            # no invented attack: every authored attack action matches some table option
            known = [(ta["attack_bonus"], [dmg(x, y) for x, y in opt]) for ta in t["attacks"] for opt in ta["damage_options"]]
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
        self.assertEqual(opts("giant-badger"), {"bite_claws": ["bite", "claws"]})
        self.assertEqual(opts("gargoyle"), {"bite_claws": ["bite", "claws"]})
        self.assertEqual(opts("animated-armor"), {"slam_slam": ["slam", "slam"]})
        self.assertEqual(opts("scout"), {"melee": ["shortsword"] * 2, "ranged": ["longbow"] * 2})
        self.assertEqual(opts("spy"), {"shortsword_x2": ["shortsword"] * 2})
        self.assertEqual(opts("thug"), {"mace_x2": ["mace"] * 2})
        self.assertEqual(opts("bandit-captain"), {"melee": ["scimitar", "scimitar", "dagger"], "ranged": ["dagger_thrown"] * 2})
        self.assertEqual(opts("veteran"), {"longsword_shortsword": ["longsword", "longsword", "shortsword"],
                                           "longsword_two_handed": ["longsword_two_handed"] * 2})
        self.assertEqual(opts("knight"), {"greatsword_x2": ["greatsword"] * 2})
        self.assertEqual(len(opts("gladiator")), 5)   # three melee attacks (any mix of spear and shield bash) or two thrown spears
        self.assertEqual(opts("gladiator")["ranged"], ["spear_thrown"] * 2)
        # Batch 3b: Life Drain in place of one longsword; only one bite; 1d4 touches (count_dice); two bites
        self.assertEqual(opts("wight"), {"longsword_x2": ["longsword"] * 2, "longsword_two_handed_x2": ["longsword_two_handed"] * 2,
                                         "longbow_x2": ["longbow"] * 2, "life_drain_longsword": ["life_drain", "longsword"],
                                         "life_drain_longsword_two_handed": ["life_drain", "longsword_two_handed"]})
        self.assertEqual(opts("vampire-spawn"), {"claws_x2": ["claws"] * 2, "claws_bite": ["claws", "bite"], "bite_claws": ["bite", "claws"]})
        self.assertEqual(opts("violet-fungus"), {"rotting_touch_1d4": ["rotting_touch"]})
        self.assertEqual(opts("death-dog"), {"bite_x2": ["bite"] * 2})
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
        for idx in ("jackal", "kobold", "wolf", "dire-wolf", "hyena", "tribal-warrior", "vulture", "blood-hawk"):
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
        # Cultist Dark Devotion: save_advantage vs charmed / frightened, the cult fanatic shape
        self.assertEqual({t["id"]: t for t in load("cultist")["traits"]}["dark_devotion"]["effect"],
                         {"kind": "save_advantage", "save_vs_conditions": ["charmed", "frightened"]})
        # Batch 2: Parry (bandit captain / knight +2), goat Charge
        for idx, b in (("bandit-captain", 2), ("knight", 2), ("gladiator", 3)):
            self.assertEqual(load(idx)["reactions"][0]["effect"], {"kind": "ac_bonus", "bonus": b})
        goat = {t["id"]: t for t in load("goat")["traits"]}
        self.assertEqual(goat["charge"]["effect"]["effects"][1], {"kind": "save_condition", "save": {"ability": "str", "dc": 10}, "apply": [{"condition": "prone"}]})
        self.assertEqual(load("giant-fire-beetle")["emits_light"], {"bright_ft": 10, "dim_ft": 10})
        # partial, description only: Sure-Footed, Brave, Leadership, Shriek, Flyby, Blood Frenzy, Mimicry
        self.assertNotIn("effect", goat["sure_footed"])
        for idx in ("knight", "gladiator"):
            self.assertNotIn("effect", {t["id"]: t for t in load(idx)["traits"]}["brave"])
        self.assertNotIn("effect", {t["id"]: t for t in load("owl")["traits"]}["flyby"])
        self.assertNotIn("effect", {t["id"]: t for t in load("flying-snake")["traits"]}["flyby"])
        self.assertNotIn("effect", {t["id"]: t for t in load("giant-owl")["traits"]}["flyby"])
        self.assertNotIn("effect", {t["id"]: t for t in load("giant-owl")["traits"]}["keen_hearing_and_sight"])
        self.assertEqual(load("giant-owl")["skills"], {"perception": 5, "stealth": 4})
        self.assertEqual(load("giant-owl")["languages"],
                         ["Giant Owl", "understands Common, Elvish, and Sylvan but can't speak"])
        self.assertNotIn("effect", {t["id"]: t for t in load("giant-bat")["traits"]}["echolocation"])
        self.assertNotIn("effect", {t["id"]: t for t in load("giant-bat")["traits"]}["keen_hearing"])
        self.assertEqual(load("giant-bat")["actions"][0]["natural"]["ability"], "str")
        self.assertNotIn("effect", {t["id"]: t for t in load("giant-badger")["traits"]}["keen_smell"])
        self.assertNotIn("effect", {t["id"]: t for t in load("giant-frog")["traits"]}["amphibious"])
        self.assertNotIn("effect", {t["id"]: t for t in load("giant-frog")["traits"]}["standing_leap"])
        # Flying Snake: automatic poison is a second damage component, not a save (SRD: no save)
        fsb = load("flying-snake")["actions"][0]
        self.assertEqual(fsb["damage"], [{"flat": 1, "type": "piercing"}, {"dice": "3d4", "type": "poison"}])
        self.assertNotIn("on_hit", fsb)
        # Giant Crab: grappled only, two claws (max_grapples 2). Do not set restrains (unlike giant toad).
        claw = load("giant-crab")["actions"][0]
        self.assertEqual(claw["on_hit"], [{"kind": "grapple", "escape_dc": 11, "max_grapples": 2}])
        self.assertNotIn("restrains", claw["on_hit"][0])
        self.assertNotIn("effect", {t["id"]: t for t in load("giant-crab")["traits"]}["amphibious"])
        # Stirge: the attack is data; attach / ongoing drain / detach stay in the description (no attach effect)
        drain = load("stirge")["actions"][0]
        self.assertEqual((drain["id"], drain["to_hit"], drain["damage"]),
                         ("blood_drain", 5, [{"dice": "1d4+3", "type": "piercing"}]))
        self.assertNotIn("on_hit", drain)
        self.assertNotIn("effect", drain)
        self.assertNotIn("effect", {t["id"]: t for t in load("quipper")["traits"]}["blood_frenzy"])
        self.assertNotIn("effect", {t["id"]: t for t in load("raven")["traits"]}["mimicry"])
        # Gladiator (CONTENT_CONVENTIONS 21): Shield Bash size-gated prone rider, thrown spear damage_override, Brute melee only
        gl = {a["id"]: a for a in load("gladiator")["actions"]}
        self.assertEqual(gl["shield_bash"]["on_hit"], [{"kind": "save_condition", "save": {"ability": "str", "dc": 15}, "apply": [{"condition": "prone", "max_size": "medium"}]}])
        self.assertEqual(gl["spear_thrown"]["damage_override"]["dice"], "2d6+4")
        self.assertEqual({t["id"]: t for t in load("gladiator")["traits"]}["brute"]["effect"], {"kind": "weapon_extra_dice", "dice": 1, "melee_only": True})
        self.assertNotIn("effect", {t["id"]: t for t in load("mule")["traits"]}["sure_footed"])
        # Mule: approved attack_bonus override, printed +2 over the derived +4 (DEVIATIONS)
        self.assertEqual({a["id"]: a for a in load("mule")["actions"]}["hooves"]["attack_bonus"]["value"], 2)
        self.assertEqual({a["id"]: a for a in load("knight")["actions"]}["leadership"]["kind"], "special")
        self.assertNotIn("effect", {t["id"]: t for t in load("shrieker")["traits"]}["shriek"])
        # Flat damage (CONTENT_CONVENTIONS 19): rat bite, scorpion sting (+ Con DC 9 1d8 poison rider, half)
        self.assertEqual(load("rat")["actions"][0]["damage"], [{"flat": 1, "type": "piercing"}])
        sting = load("scorpion")["actions"][0]
        self.assertEqual(sting["damage"], [{"flat": 1, "type": "piercing"}])
        self.assertEqual(sting["on_hit"], [{"kind": "save_condition", "save": {"ability": "con", "dc": 9},
                                            "damage": [{"dice": "1d8", "type": "poison"}], "on_success": "half"}])
        # Batch 3: ghast claws paralysis (undead exempt, no elf clause), approved bite override; Stench / Turn Defiance / Magic Resistance text only
        gh = {a["id"]: a for a in load("ghast")["actions"]}
        self.assertEqual(gh["bite"]["attack_bonus"]["value"], 3)
        self.assertEqual(gh["claws"]["on_hit"][0]["when"], [{"kind": "not_creature_type", "creature_types": ["undead"]}])
        self.assertEqual(gh["claws"]["on_hit"][0]["apply"][0]["condition"], "paralyzed")
        self.assertEqual({t["id"]: t for t in load("ghast")["traits"]}["stench"]["effect"], {"kind": "aura", "radius_ft": 5, "on": "start_turn", "save": {"ability": "con", "dc": 10},
                         "condition": "poisoned", "duration": "until_start_of_next_turn", "success_immunity_hours": 24})
        for idx, tid in (("ghast", "turn_defiance"),):
            self.assertNotIn("effect", {t["id"]: t for t in load(idx)["traits"]}[tid])
        # Batch 3b riders (feat/monster-batch3-riders, feat/monster-strength-drain)
        def trait(idx, tid):
            return {t["id"]: t for t in load(idx)["traits"]}[tid]
        def act(idx, aid):
            return {a["id"]: a for a in load(idx)["actions"]}[aid]
        # CR 0.25 batch B. Constrictor: giant-toad grapple (restrains + exclusive). Str/Dex tie, explicit str.
        constrict = act("constrictor-snake", "constrict")
        self.assertEqual(constrict["on_hit"], [{"kind": "grapple", "escape_dc": 14, "restrains": True, "exclusive": True}])
        self.assertEqual(act("constrictor-snake", "bite").get("ability"), "str")
        self.assertEqual(constrict.get("ability"), "str")
        # Elk: Charge is the ram's on_hit only (a melee on_event would also fire on hooves). Hooves has no prone gate.
        self.assertNotIn("effect", trait("elk", "charge"))
        self.assertEqual(act("elk", "ram")["on_hit"], [
            {"kind": "extra_damage", "damage": [{"dice": "2d6", "type": "bludgeoning"}],
             "when": [{"kind": "moved_at_least_ft", "ft": 20}]},
            {"kind": "save_condition", "save": {"ability": "str", "dc": 13}, "apply": [{"condition": "prone"}],
             "when": [{"kind": "moved_at_least_ft", "ft": 20}]}])
        hooves = act("elk", "hooves")
        self.assertNotIn("on_hit", hooves)
        self.assertNotIn("target_requires_any", hooves)
        self.assertIn("one prone creature", hooves["description"])
        # Panther: one slug (Leopard / Cougar are aliases, not documents). Pounce prone save is on the claw.
        # The bonus-action bite vs the prone target is description only (no prone gate on bonus_attack).
        self.assertEqual(load("panther")["id"], "srd51.monster.panther")
        self.assertFalse((PACK / "monsters" / "leopard.json").exists())
        self.assertFalse((PACK / "monsters" / "cougar.json").exists())
        self.assertNotIn("effect", trait("panther", "pounce"))
        self.assertNotIn("effect", trait("panther", "keen_smell"))
        self.assertIn("bonus action", trait("panther", "pounce")["description"])
        self.assertEqual(act("panther", "claw")["on_hit"], [
            {"kind": "save_condition", "save": {"ability": "str", "dc": 12}, "apply": [{"condition": "prone"}],
             "when": [{"kind": "moved_at_least_ft", "ft": 20}]}])
        self.assertNotIn("on_hit", act("panther", "bite"))
        # Flying swarms: swarm tag + printed fly (not a walk stand-in). Half-HP dice. Reach as printed (bats 0, ravens 5).
        bats = load("swarm-of-bats")
        self.assertEqual((bats["tags"], bats["speed"]), (["swarm"], {"walk": 0, "fly": 30}))
        self.assertEqual(act("swarm-of-bats", "bites")["natural"]["damage_ability"], "none")
        self.assertEqual((act("swarm-of-bats", "bites")["reach"], act("swarm-of-bats", "bites")["half_hp_damage"]), (0, "1d4"))
        self.assertNotIn("effect", trait("swarm-of-bats", "swarm"))
        self.assertNotIn("effect", trait("swarm-of-bats", "echolocation"))
        self.assertNotIn("effect", trait("swarm-of-bats", "keen_hearing"))
        ravens = load("swarm-of-ravens")
        self.assertEqual((ravens["tags"], ravens["speed"], ravens["skills"]["perception"]), (["swarm"], {"walk": 10, "fly": 50}, 5))
        self.assertEqual(act("swarm-of-ravens", "beaks")["natural"]["damage_ability"], "none")
        self.assertEqual((act("swarm-of-ravens", "beaks")["reach"], act("swarm-of-ravens", "beaks")["half_hp_damage"]), (5, "1d6"))
        self.assertNotIn("effect", trait("swarm-of-ravens", "swarm"))
        self.assertEqual(trait("satyr", "magic_resistance")["effect"], {"kind": "save_advantage", "magical": True})
        self.assertEqual(trait("death-dog", "two_headed")["effect"], {"kind": "save_advantage", "save_vs_conditions":
                         ["blinded", "charmed", "deafened", "frightened", "stunned", "unconscious"]})
        self.assertEqual(act("death-dog", "bite")["on_hit"], [{"kind": "save_condition", "save": {"ability": "con", "dc": 12},
                         "apply": [{"condition": "poisoned", "duration": {"kind": "until_cured"}}]}])
        self.assertEqual(act("wight", "life_drain")["drain"], {"max_hp": True, "save": {"ability": "con", "dc": 13}})
        vb = act("vampire-spawn", "bite")
        self.assertEqual(vb["drain"], {"max_hp": True, "damage_type": "necrotic", "heal_self": True})
        self.assertEqual(vb["target_requires_any"], ["willing", "grappled_by_self", "incapacitated", "restrained"])
        self.assertEqual(act("vampire-spawn", "claws")["on_hit"],
                         [{"kind": "grapple", "escape_dc": 13, "instead_of_damage": True}])   # grapple-instead (§27)
        self.assertEqual(act("shadow", "strength_drain")["ability_drain"], {"ability": "str", "dice": "1d4"})
        self.assertEqual(act("violet-fungus", "rotting_touch")["attack_bonus"]["value"], 2)
        for idx, tid in (("wight", "sunlight_sensitivity"), ("vampire-spawn", "regeneration"), ("vampire-spawn", "vampire_weaknesses"),
                         ("shadow", "shadow_stealth"), ("shadow", "sunlight_weakness")):
            self.assertNotIn("effect", trait(idx, tid))
        # CONTENT_CONVENTIONS 23: Str/Dex ties carry an explicit ability (thrown uses Str; a non-finesse longbow uses Dex)
        for idx, aid, ab in (("black-dragon-wyrmling", "bite", "str"), ("bugbear", "morningstar", "str"), ("bugbear", "javelin", "str"),
                             ("bugbear", "javelin_thrown", "str"), ("commoner", "club", "str"), ("guard", "spear", "str"),
                             ("guard", "spear_two_handed", "str"), ("guard", "spear_thrown", "str"), ("hobgoblin", "longsword", "str"),
                             ("hobgoblin", "longsword_two_handed", "str"), ("hobgoblin", "longbow", "dex"),
                             ("wight", "life_drain", "str"), ("wight", "longsword", "str"), ("wight", "longsword_two_handed", "str"),
                             ("wight", "longbow", "dex"), ("vampire-spawn", "bite", "str"), ("vampire-spawn", "claws", "str"),
                             ("death-dog", "bite", "str"),
                             ("constrictor-snake", "bite", "str"), ("constrictor-snake", "constrict", "str"),
                             ("panther", "bite", "str"), ("panther", "claw", "str")):
            self.assertEqual(act(idx, aid).get("ability"), ab, (idx, aid))
        # Sunlight Sensitivity / Keen Hearing and Smell: descriptive (no light model, no perception checks in the sim)
        self.assertNotIn("effect", {t["id"]: t for t in load("kobold")["traits"]}["sunlight_sensitivity"])

    def test_cr_quarter_batch_c(self):
        # Bestiary v0.1 CR 1/4 batch C. Poison fail-by-5 / result-5 unconscious stays in the description:
        # also_on_fail_by is save_effect-only (CONTENT_CONVENTIONS 26). Innate casting is not a slot spellcasting entry.
        def trait(idx, tid):
            return {t["id"]: t for t in load(idx)["traits"]}[tid]

        def act(idx, aid):
            return {a["id"]: a for a in load(idx)["actions"]}[aid]

        hour = {"condition": "poisoned", "duration": {"kind": "rounds", "n": 600}}
        minute = {"condition": "poisoned", "duration": {"kind": "rounds", "n": 10}}
        drow = load("drow")
        self.assertEqual(drow["tags"], ["elf"])
        self.assertEqual(drow["senses"]["darkvision"], 120)
        self.assertNotIn("spellcasting", drow)
        self.assertEqual(trait("drow", "fey_ancestry")["effect"],
                         {"kind": "save_advantage", "save_vs_conditions": ["charmed"]})
        self.assertNotIn("effect", trait("drow", "sunlight_sensitivity"))
        self.assertNotIn("effect", trait("drow", "innate_spellcasting"))
        hx = act("drow", "hand_crossbow")
        self.assertEqual((hx["weapon"], hx["range"]), ("srd51.item.crossbow_hand", [30, 120]))
        self.assertEqual(hx["on_hit"], [{"kind": "save_condition", "save": {"ability": "con", "dc": 13}, "apply": [hour]}])
        self.assertNotIn("also_on_fail_by", hx)
        self.assertNotIn("also_on_fail_by", hx["on_hit"][0])
        self.assertEqual(load("pseudodragon")["natural_armor"], {"base": 11, "dex": True})
        self.assertEqual(load("pseudodragon")["hands"], 0)
        self.assertEqual(trait("pseudodragon", "magic_resistance")["effect"], {"kind": "save_advantage", "magical": True})
        self.assertNotIn("effect", trait("pseudodragon", "keen_senses"))
        self.assertNotIn("effect", trait("pseudodragon", "limited_telepathy"))
        sting = act("pseudodragon", "sting")
        self.assertEqual(sting["on_hit"], [{"kind": "save_condition", "save": {"ability": "con", "dc": 11}, "apply": [hour]}])
        self.assertNotIn("also_on_fail_by", sting["on_hit"][0])
        self.assertEqual(act("sprite", "longsword")["attack_bonus"]["value"], 2)
        self.assertEqual(act("sprite", "longsword")["natural"], {"flat": 1, "ability": "str"})
        self.assertNotIn("weapon", act("sprite", "longsword"))
        bow = act("sprite", "shortbow")
        self.assertEqual(bow["natural"], {"flat": 1, "ability": "dex"})
        self.assertEqual(bow["range"], [40, 160])
        self.assertNotIn("weapon", bow)
        self.assertEqual(bow["on_hit"], [{"kind": "save_condition", "save": {"ability": "con", "dc": 10}, "apply": [minute]}])
        self.assertNotIn("also_on_fail_by", bow["on_hit"][0])
        self.assertEqual(act("sprite", "heart_sight")["kind"], "special")
        self.assertNotIn("effect", act("sprite", "heart_sight"))
        self.assertEqual(act("sprite", "invisibility")["kind"], "special")
        self.assertNotIn("effect", act("sprite", "invisibility"))
        self.assertNotIn("spellcasting", load("sprite"))
        meph = load("steam-mephit")
        self.assertEqual(meph["hands"], 0)
        self.assertNotIn("spellcasting", meph)
        self.assertNotIn("effect", trait("steam-mephit", "death_burst"))
        self.assertNotIn("effect", trait("steam-mephit", "innate_spellcasting"))
        claws = act("steam-mephit", "claws")
        self.assertEqual(claws["natural"], {"dice": "1d4", "ability": "dex", "damage_ability": "none"})
        self.assertEqual(claws["ability"], "dex")
        breath = act("steam-mephit", "steam_breath")
        self.assertEqual((breath["kind"], breath["save"], breath["on_success"], breath["usage"]),
                         ("save_effect", {"ability": "dex", "dc": 10}, "half", {"kind": "recharge", "min": 6}))
        self.assertEqual(breath["area"], {"shape": "cone", "size_ft": 15})
        self.assertEqual(breath["damage"], [{"dice": "1d8", "type": "fire"}])

    def test_cr_half_batch_e(self):
        # Bestiary v0.1 CR 1/2 batch E. Death Burst and innate stay descriptive (as steam mephit).
        # Dust Blinding Breath is a real save_effect apply (no damage). Cockatrice petrify is not an on_hit
        # stand-in: escalates is gaze-only. Sahuagin Blood Frenzy is description only (as quipper).

        def trait(idx, tid):
            return {t["id"]: t for t in load(idx)["traits"]}[tid]

        def act(idx, aid):
            return {a["id"]: a for a in load(idx)["actions"]}[aid]

        minute = {"condition": "blinded", "duration": {"kind": "rounds", "n": 10}, "repeat_save": "end_of_turn"}
        for idx in ("ice-mephit", "dust-mephit", "magma-mephit"):
            meph = load(idx)
            self.assertEqual(meph["hands"], 0)
            self.assertNotIn("spellcasting", meph)
            self.assertNotIn("effect", trait(idx, "death_burst"))
            self.assertNotIn("effect", trait(idx, "innate_spellcasting"))
        self.assertNotIn("effect", trait("ice-mephit", "false_appearance"))
        self.assertNotIn("effect", trait("magma-mephit", "false_appearance"))
        self.assertNotIn("false_appearance", {t["id"] for t in load("dust-mephit")["traits"]})
        self.assertEqual(act("ice-mephit", "claws")["natural"],
                         {"dice": "1d4", "ability": "dex", "damage_ability": "dex"})
        self.assertEqual(act("dust-mephit", "claws")["natural"],
                         {"dice": "1d4", "ability": "dex", "damage_ability": "dex"})
        self.assertEqual(act("magma-mephit", "claws")["natural"],
                         {"dice": "1d4", "ability": "dex", "damage_ability": "dex"})
        frost = act("ice-mephit", "frost_breath")
        self.assertEqual((frost["kind"], frost["save"], frost["on_success"], frost["usage"]),
                         ("save_effect", {"ability": "dex", "dc": 10}, "half", {"kind": "recharge", "min": 6}))
        self.assertEqual(frost["area"], {"shape": "cone", "size_ft": 15})
        self.assertEqual(frost["damage"], [{"dice": "2d4", "type": "cold"}])
        self.assertNotIn("apply", frost)
        blind = act("dust-mephit", "blinding_breath")
        self.assertEqual((blind["kind"], blind["save"], blind["usage"]),
                         ("save_effect", {"ability": "dex", "dc": 10}, {"kind": "recharge", "min": 6}))
        self.assertEqual(blind["area"], {"shape": "cone", "size_ft": 15})
        self.assertNotIn("damage", blind)
        self.assertNotIn("on_success", blind)
        self.assertEqual(blind["apply"], [minute])
        fire = act("magma-mephit", "fire_breath")
        self.assertEqual((fire["kind"], fire["save"], fire["on_success"], fire["usage"]),
                         ("save_effect", {"ability": "dex", "dc": 11}, "half", {"kind": "recharge", "min": 6}))
        self.assertEqual(fire["area"], {"shape": "cone", "size_ft": 15})
        self.assertEqual(fire["damage"], [{"dice": "2d6", "type": "fire"}])
        bite = act("cockatrice", "bite")
        self.assertEqual(bite["natural"], {"dice": "1d4", "ability": "dex"})
        self.assertEqual(bite["damage"], [{"dice": "1d4+1", "type": "piercing"}])
        self.assertNotIn("on_hit", bite)
        self.assertEqual(load("cockatrice")["speed"], {"walk": 20, "fly": 40})
        self.assertIn("petrified for 24 hours", bite["description"])
        sah = load("sahuagin")
        self.assertEqual((sah["tags"], sah["speed"], sah["natural_armor"]),
                         (["sahuagin"], {"walk": 30, "swim": 40}, {"base": 12, "dex": True}))
        self.assertNotIn("effect", trait("sahuagin", "blood_frenzy"))
        self.assertNotIn("effect", trait("sahuagin", "limited_amphibiousness"))
        self.assertNotIn("effect", trait("sahuagin", "shark_telepathy"))
        self.assertIn("4 hours", trait("sahuagin", "limited_amphibiousness")["description"])
        self.assertNotIn("pack_tactics", [t.get("effect", {}).get("kind") for t in sah["traits"]])
        opts = {o["id"]: o["attacks"] for t in sah["traits"] if t.get("effect", {}).get("kind") == "multiattack"
                for o in t["effect"]["options"]}
        self.assertEqual(opts, {"bite_claws": ["bite", "claws"], "bite_spear": ["bite", "spear"]})
        self.assertEqual(act("sahuagin", "spear")["weapon"], "srd51.item.spear")
        self.assertEqual(act("sahuagin", "spear_two_handed")["use"], "two_hands")
        self.assertEqual(act("sahuagin", "spear_thrown")["range"], [20, 60])

    def test_cr_half_batch_d(self):
        # CR 1/2 batch D. Charge is the goat trait on_event (not elk's ram on_hit). Warhorse is that shape
        # with the prone save only. Wasp poison is the giant spider bite rider. Insects is bites only.

        def trait(idx, tid):
            return {t["id"]: t for t in load(idx)["traits"]}[tid]

        def act(idx, aid):
            return {a["id"]: a for a in load(idx)["actions"]}[aid]

        when = [{"kind": "weapon", "is": "melee"}, {"kind": "moved_at_least_ft", "ft": 20}]
        goat = trait("giant-goat", "charge")["effect"]
        self.assertEqual((goat["kind"], goat["on"], goat["limit"], goat["when"]), ("on_event", "hit", "once_per_turn", when))
        self.assertEqual(goat["effects"], [
            {"kind": "extra_damage", "damage": [{"dice": "2d4", "type": "bludgeoning"}]},
            {"kind": "save_condition", "save": {"ability": "str", "dc": 13}, "apply": [{"condition": "prone"}]}])
        self.assertNotIn("effect", trait("giant-goat", "sure_footed"))
        self.assertEqual(act("giant-goat", "ram")["natural"], {"dice": "2d4", "ability": "str"})
        self.assertNotIn("on_hit", act("giant-goat", "ram"))
        sea = trait("giant-sea-horse", "charge")["effect"]
        self.assertEqual((sea["kind"], sea["on"], sea["limit"], sea["when"]), ("on_event", "hit", "once_per_turn", when))
        self.assertEqual(sea["effects"], [
            {"kind": "extra_damage", "damage": [{"dice": "2d6", "type": "bludgeoning"}]},
            {"kind": "save_condition", "save": {"ability": "str", "dc": 11}, "apply": [{"condition": "prone"}]}])
        self.assertEqual(load("giant-sea-horse")["speed"], {"walk": 0, "swim": 40})
        self.assertNotIn("effect", trait("giant-sea-horse", "water_breathing"))
        sting = act("giant-wasp", "sting")
        self.assertEqual(sting["on_hit"], [{"kind": "save_condition", "save": {"ability": "con", "dc": 11},
                                            "damage": [{"dice": "3d6", "type": "poison"}], "on_success": "half",
                                            "zero_hp": {"condition": "poisoned", "while": "paralyzed",
                                                        "duration": {"kind": "rounds", "n": 600}}}])
        self.assertEqual(load("giant-wasp")["speed"], {"walk": 10, "fly": 50})
        self.assertNotIn("swim", load("giant-wasp")["speed"])
        insects = load("swarm-of-insects")
        self.assertEqual((insects["tags"], insects["speed"]), (["swarm"], {"walk": 20, "climb": 20}))
        bites = act("swarm-of-insects", "bites")
        self.assertEqual(bites["natural"], {"dice": "4d4", "ability": "dex", "damage_ability": "none"})
        self.assertEqual((bites["reach"], bites["half_hp_damage"], bites["damage"]),
                         (0, "2d4", [{"dice": "4d4", "type": "piercing"}]))
        self.assertNotIn("on_hit", bites)
        self.assertNotIn("effect", trait("swarm-of-insects", "swarm"))
        # Variant sidebars (beetle / centipede / spider / wasp) are out of scope: one bites-only document.
        for slug in ("swarm_of_beetles", "swarm_of_centipedes", "swarm_of_spiders", "swarm_of_wasps"):
            self.assertFalse((PACK / "monsters" / f"{slug}.json").exists(), slug)
        horse = trait("warhorse", "trampling_charge")["effect"]
        self.assertEqual((horse["kind"], horse["on"], horse["limit"], horse["when"]), ("on_event", "hit", "once_per_turn", when))
        self.assertEqual(horse["effects"], [
            {"kind": "save_condition", "save": {"ability": "str", "dc": 14}, "apply": [{"condition": "prone"}]}])
        self.assertNotIn("extra_damage", str(horse["effects"]))
        self.assertIn("bonus action", trait("warhorse", "trampling_charge")["description"])
        self.assertNotIn("on_hit", act("warhorse", "hooves"))
        self.assertEqual(act("warhorse", "hooves")["natural"], {"dice": "2d6", "ability": "str"})

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
        # Giant Frog: the toad pattern (Rules #189), Small or smaller, 5 (2d4) acid, bite grapple DC 11
        frog = {a["id"]: a for a in load("giant-frog")["actions"]}
        self.assertEqual(frog["bite"]["on_hit"], [{"kind": "grapple", "escape_dc": 11, "restrains": True, "exclusive": True}])
        fr = frog["swallow"]
        self.assertEqual((fr["kind"], fr["attack"], fr["to_hit"], fr["range_ft"]), ("attack_effect", "melee", 3, 5))
        self.assertEqual(fr["apply"], [{"condition": "restrained", "max_size": "small",
                                        "contain": {"kind": "swallow", "damage": {"dice": "2d4", "type": "acid"}}}])

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
