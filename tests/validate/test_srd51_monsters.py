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
# Deferred (docs/SRD_MONSTERS_DEFERRED.md "Deferred", 173 authored / 7 deferred): batch 3 and tier-A triage (3: mimic, doppelganger, green hag). Quipper left the deferred list (Blood Frenzy description-only, swim-only walk 0). Giant Crab left once max_grapples landed (Druid gap G). Constrictor Snake and Elk left in CR 0.25 batch B. Giant Frog left in batch A (Swallow pattern); it stays authored and is not restored from batch B's stale deferred row. Giant Goat, Giant Sea Horse and Warhorse left in CR 1/2 batch D. CR 1/2 batch E (ice mephit, dust mephit, magma mephit, cockatrice, sahuagin) is authored; Cockatrice petrify and Sahuagin Blood Frenzy stay partial on those rows, not new deferred monsters. Crocodile and Rust Monster left in CR 1/2 batch F (explicit partials, not a silent re-defer). Lion and tiger left in CR 1 batch G (explicit partials, same bonus-bite gap as panther, not a silent re-defer). Giant eagle, giant vulture and hippogriff are authored in that batch; none of them has Flyby on the SRD block. Duergar left in CR 1 batch H (Enlarge, Invisibility, and Duergar Resilience stay partials on the authored monster). CR 2 batch I authored polar bear, giant boar, giant elk, rhinoceros, saber-toothed tiger and hunter shark; polar bear is complete, and the other five stay partials on the authored rows (straight-toward, elk hooves, tiger bonus bite, quipper Blood Frenzy). CR 2 batch J authored centaur, griffon, pegasus, ankheg, giant constrictor snake and minotaur skeleton; Centaur Charge is pike on_hit only (not a melee on_event). Minotaur Skeleton's 10-foot push stays description only. CR 2 batch K authored druid, priest, merrow and wererat (druid's four missing spells stay in the trait text only; priest Divine Eminence is text only and Religion +4 is the printed total; merrow Pull is text only; wererat is the humanoid form, with shapechange, bite and the curse as text). Sea hag is a new deferral. Mimic stays deferred. CR 2 batch L authored grick, swarm of poisonous snakes, gibbering mouther, will-o'-wisp and ochre jelly. Grick's beak-if-tentacles-hit gate stays in the Multiattack description. Gibbering Mouther Blinding Spittle is a point-origin sphere save_effect (recharge 5-6). Will-o'-wisp Consume Life, Ephemeral, Incorporeal Movement, Variable Illumination and Invisibility stay description only on the authored monster. Ochre Jelly Split stays a description-only trait. CR 3 batch M authored giant scorpion, minotaur, hell hound and winter wolf (no new deferrals). Minotaur has no Multiattack (one greataxe or one gore). CR 3 batch N authored manticore, werewolf and phase spider. Manticore Tail Spike Regrowth spike count is description only and the row stays green. Werewolf is the humanoid form (a6212be werewolf-human); hybrid/wolf AC 12, wolf speed 40, bite, claws and the lycanthropy curse stay description only. Phase spider Ethereal Jaunt stays description only (no Ethereal Plane). Stealth +6 is expertise (Dex +2 and 2×PB). CR 4 batch O authored black pudding, elephant and ettin. Black Pudding Corrosive Form contact acid, weapon and armor corrosion, and Split stay description only; condition immunities follow the SRD line (deafened) over a6212be. Elephant Stomp stays description only (no prone-target gate). Ettin battleaxe and morningstar are naturals at the printed 2d8. Chuul and ghost are new deferrals. CR 4 batch P authored wereboar, weretiger, lamia and couatl. Wereboar and weretiger are the humanoid forms (a6212be wereboar-human / weretiger-human); hybrid and animal forms, tusks, bite, claws, lycanthropy and Charge/Pounce stay description only. Wereboar Relentless is live (damage_at_most 14, per_rest short) and keeps the SRD recharge name (a6212be omits it from the trait text, as giant boar). Lamia Innate Spellcasting and Intoxicating Touch stay description only (claws + dagger is the authored multiattack). Couatl Magic Weapons is {kind: magic_weapons}. Bite poisoned 24 hours (14400 rounds) is a save_condition; unconscious-while-poisoned and the wake action stay description only (no zero_hp.while). Constrict grapple restrains and is exclusive; the Medium-or-smaller gate stays in the text. Change Shape does not set shapechanger. Succubus is parked. Sea hag, mimic, doppelganger, green hag, succubus, chuul and ghost stay deferred.
DEFERRED: list[str] = ["chuul", "doppelganger", "ghost", "green-hag", "mimic", "sea-hag", "succubus"]   # docs/SRD_MONSTERS_DEFERRED.md: 173 / 7
# Authored attacks with no table damage option: the table copies one damage line per attack, the SRD text has a second.
EXTRA_ATTACKS = {("bugbear", "javelin_thrown"): "SRD: 5 (1d6 + 2) piercing damage at range; the table lists only the melee 2d6 + 2."}
# Table attacks kept verbatim that are not weapon actions. Elephant Stomp is a bonus action
# against one prone creature; there is no prone-target gate, so it is not an ungated action.
DESCRIPTION_ONLY_ATTACKS = {
    ("elephant", "Stomp"): "bonus action vs one prone creature; description-only on Trampling Charge",
}
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
    "specter": "damage_resistances lists the five plain types: the qualified 'nonmagical weapons' entry lives in the damage_mitigation trait effect (as shadow). speed.hover is the printed fly hover flag.",
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
    "swarm-of-quippers": "type: as swarm-of-rats (beast + tag swarm).",
    "crocodile": "speed: a6212be lists walk 20 ft. and swim 20 ft. SRD 5.1 prints 20 ft., swim 30 ft. Authored as swim 30 (the SRD is authoritative, as giant-wasp).",
    "deep-gnome": "xp: the table lists 50 XP; SRD 5.1 prints Challenge 1/2 (100 XP), and the SRD is authoritative (CR 1/2 = 100 XP, as riding-horse). One stat block: the a6212be index is deep-gnome-svirfneblin; Svirfneblin is an alias of srd51.monster.deep_gnome, not a separate document.",
    "polar-bear": "One SRD 5.1 stat block. Cave Bear is an alias of srd51.monster.polar_bear, not a separate document.",
    "saber-toothed-tiger": "Bite and Claw damage: the SRD prints +5 (1d10 + 5 / 2d6 + 5); Str 18 derives +4. Carried as an approved damage_override (CONTENT_CONVENTIONS 21; printed value wins). To-hit +6 derives (Str +4 and PB +2) and needs no attack_bonus.",
    "wererat": "damage_immunities stays empty: the qualified 'nonmagical weapons that aren't silvered' entry lives in the damage_mitigation trait effect (immunities.nonmagical_not_silvered, CONTENT_EFFECTS werewolf example; as gargoyle, but an immunity). The pin is the humanoid form (a6212be wererat-human); bite is not a weapon attack on that form.",
    "priest": "skills.religion is the SRD print +4. Int 13 (+1) with PB 2 is proficiency +3 or expertise +5; neither matches. validate_packs.PRINTED_SKILL_TOTALS allowlists this one printed total. Medicine +7 is expertise and Persuasion +3 is proficiency.",
    "grick": "damage_resistances stays empty: the qualified 'nonmagical weapons' entry lives in the damage_mitigation trait effect (as specter).",
    "will-o-wisp": "damage_resistances lists the five plain types: the qualified 'nonmagical weapons' entry lives in the damage_mitigation trait effect (as specter). speed.hover is the printed fly hover flag. Shock to_hit: the SRD prints +4 (the table agrees); Dex 28 + PB 2 = +11. Carried as an approved attack_bonus override of +4 (printed value wins).",
    "ochre-jelly": "condition_immunities: the table lists blinded twice and omits deafened; the SRD line is blinded, charmed, deafened, exhaustion, frightened, prone (as gray ooze / gelatinous cube; same mirror shape as shrieker).",
    "werewolf": "damage_immunities stays empty: the qualified 'nonmagical weapons that aren't silvered' entry lives in the damage_mitigation trait effect (immunities.nonmagical_not_silvered, as wererat). The pin is the humanoid form (a6212be werewolf-human); bite and claws are not weapon attacks on that form.",
    "wereboar": "damage_immunities stays empty: the qualified 'nonmagical weapons that aren't silvered' entry lives in the damage_mitigation trait effect (immunities.nonmagical_not_silvered, as wererat). The pin is the humanoid form (a6212be wereboar-human); tusks are not a weapon attack on that form.",
    "weretiger": "damage_immunities stays empty: the qualified 'nonmagical weapons that aren't silvered' entry lives in the damage_mitigation trait effect (immunities.nonmagical_not_silvered, as wererat). The pin is the humanoid form (a6212be weretiger-human); bite and claw are not weapon attacks on that form.",
    "couatl": "damage_immunities lists psychic only: the qualified 'nonmagical weapons' entry lives in the damage_mitigation trait effect (immunities.nonmagical, the mummy/specter qualifier, as an immunity). Bite to_hit: the SRD prints +8 (the table agrees); Dex 20 (+5) + PB 2 = +7. Constrict to_hit: the SRD prints +6 (the table agrees); Str 16 (+3) + PB 2 = +5. Both are approved attack_bonus overrides (printed value wins).",
    "black-pudding": "condition_immunities: the table omits deafened; the SRD line is blinded, charmed, deafened, exhaustion, frightened, prone (as ochre jelly / gray ooze / gelatinous cube). a6212be drops deafened rather than listing blinded twice (the ochre-jelly mirror shape).",
    "elephant": "Stomp: the table lists +8, 3d10+6 bludgeoning, one prone creature. No prone-target gate, so Stomp stays in the Trampling Charge description and is not an ungated action (unlike elk Hooves). Gore is the authored attack.",
}
# Mirror errors in hit points: monster -> (average, formula) as printed in the SRD 5.1 block (DEVIATIONS).
HP_ERRATA = {"cult-fanatic": (33, "6d8+6")}

QUALIFIED_RESISTANCES = ("gargoyle", "wight", "vampire-spawn", "shadow", "mummy", "specter", "grick", "will-o-wisp")
QUALIFIED_IMMUNITIES = ("wererat", "werewolf", "wereboar", "weretiger", "couatl")


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
                if idx in ("swarm-of-rats", "swarm-of-bats", "swarm-of-ravens", "swarm-of-insects", "swarm-of-quippers", "swarm-of-poisonous-snakes"):
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
                if idx == "deep-gnome":
                    self.assertEqual(t["xp"], 50)   # the pinned mirror value stays verbatim (DEVIATIONS)
                self.assertEqual(d["challenge"]["xp"], 100 if idx == "deep-gnome" else 50 if idx == "riding-horse" else t["xp"])
                rating = d["challenge"]["rating"]
                self.assertEqual(CR[rating] if rating in CR else int(rating), t["cr"])
                if idx == "giant-wasp":
                    # a6212be lists swim 50 ft.; SRD 5.1 prints walk 10 / fly 50 only (DEVIATIONS).
                    self.assertEqual(t["speed"], {"walk": "10 ft.", "fly": "50 ft.", "swim": "50 ft."})
                    self.assertEqual(d["speed"], {"walk": 10, "fly": 50})
                elif idx == "crocodile":
                    # a6212be lists swim 20 ft.; SRD 5.1 prints walk 20 / swim 30 (DEVIATIONS).
                    self.assertEqual(t["speed"], {"walk": "20 ft.", "swim": "20 ft."})
                    self.assertEqual(d["speed"], {"walk": 20, "swim": 30})
                else:
                    speed = {}
                    for k, v in t["speed"].items():
                        speed[k] = v if isinstance(v, bool) else num(v)   # specter: speed.hover true
                    speed.setdefault("walk", 0)   # the table has no walk key for swim-only creatures (reef shark: swim 40 ft.)
                    self.assertEqual(speed, d["speed"])
                self.assertEqual([d["abilities"][k] for k in ("str", "dex", "con", "int", "wis", "cha")], t["abilities"])
                self.assertEqual(d.get("damage_vulnerabilities", []), t["damage_vulnerabilities"])
                if idx in QUALIFIED_IMMUNITIES:   # the qualified "nonmagical weapons" entry is a damage_mitigation immunity (DEVIATIONS)
                    self.assertEqual(d.get("damage_immunities", []), [r for r in t["damage_immunities"] if "nonmagical" not in r])
                else:
                    self.assertEqual(d.get("damage_immunities", []), t["damage_immunities"])
                if idx in QUALIFIED_RESISTANCES:   # the qualified "nonmagical weapons" entry is a damage_mitigation trait (DEVIATIONS)
                    self.assertEqual(d.get("damage_resistances", []), [r for r in t["damage_resistances"] if "nonmagical" not in r])
                else:
                    self.assertEqual(d.get("damage_resistances", []), t["damage_resistances"])
                if idx in ("shrieker", "violet-fungus"):
                    self.assertEqual(d["condition_immunities"], ["blinded", "deafened", "frightened"])
                elif idx == "ochre-jelly":
                    self.assertEqual(t["condition_immunities"], ["blinded", "charmed", "blinded", "exhaustion", "frightened", "prone"])
                    self.assertEqual(d["condition_immunities"], ["blinded", "charmed", "deafened", "exhaustion", "frightened", "prone"])
                elif idx == "black-pudding":
                    self.assertEqual(t["condition_immunities"], ["blinded", "charmed", "exhaustion", "frightened", "prone"])
                    self.assertEqual(d["condition_immunities"], ["blinded", "charmed", "deafened", "exhaustion", "frightened", "prone"])
                else:
                    self.assertEqual(d.get("condition_immunities", []), t["condition_immunities"])
                self.assertEqual(d["proficiency_bonus"], 2 if t["cr"] <= 4 else 3)   # SRD: CR 0-4 +2, CR 5-8 +3 (medusa, CR 6)

    def test_every_attack_matches_the_table(self):
        for idx in AUTHORED:
            t, d = TABLE[idx], load(idx)
            attacks = [a for a in d["actions"] if a["kind"] in ("melee_weapon_attack", "ranged_weapon_attack")]
            for ta in t["attacks"]:
                if (idx, ta["name"]) in DESCRIPTION_ONLY_ATTACKS:
                    continue
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
        self.assertEqual(opts("polar-bear"), {"bite_claws": ["bite", "claws"]})
        self.assertEqual(opts("giant-badger"), {"bite_claws": ["bite", "claws"]})
        self.assertEqual(opts("giant-eagle"), {"beak_talons": ["beak", "talons"]})
        self.assertEqual(opts("giant-vulture"), {"beak_talons": ["beak", "talons"]})
        self.assertEqual(opts("hippogriff"), {"beak_claws": ["beak", "claws"]})
        self.assertEqual(opts("centaur"), {"pike_hooves": ["pike", "hooves"], "longbow_x2": ["longbow", "longbow"]})
        self.assertEqual(opts("griffon"), {"beak_claws": ["beak", "claws"]})
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
        # Pack Tactics (common@1 open kind pack_tactics, sim #127): one declaration, no fields.
        # Wolf also stamps Keen Hearing and Smell (CONTENT_CONVENTIONS §30); the others stay description-only.
        for idx in ("jackal", "kobold", "dire-wolf", "hyena", "tribal-warrior", "vulture", "blood-hawk", "lion"):
            with self.subTest(monster=idx):
                effs = [t["effect"] for t in load(idx)["traits"] if "effect" in t]
                self.assertEqual(effs, [{"kind": "pack_tactics"}])
        self.assertEqual([t["effect"] for t in load("wolf")["traits"] if "effect" in t],
                         [{"kind": "keen_senses", "senses": ["hearing", "smell"]}, {"kind": "pack_tactics"}])
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
                             ("panther", "bite", "str"), ("panther", "claw", "str"),
                             ("giant-eagle", "beak", "str"), ("giant-eagle", "talons", "str"),
                             ("rust-monster", "bite", "str"),
                             ("deep-gnome", "war_pick", "str"), ("deep-gnome", "poisoned_dart", "dex")):
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

    def test_cr_half_batch_f(self):
        # CR 1/2 batch F. Crocodile is the constrictor-snake grapple. Darkmantle attach and Darkness Aura stay text
        # (no attach construct, no magical_darkness zone). Rust Metal / Antennae / Corrode Metal stay text: the
        # manufactured_metal predicate is for when item degradation lands, and is not wired here.
        def trait(idx, tid):
            return {t["id"]: t for t in load(idx)["traits"]}[tid]

        def act(idx, aid):
            return {a["id"]: a for a in load(idx)["actions"]}[aid]

        self.assertNotIn("effect", trait("crocodile", "hold_breath"))
        self.assertEqual(act("crocodile", "bite")["on_hit"],
                         [{"kind": "grapple", "escape_dc": 12, "restrains": True, "exclusive": True}])
        self.assertEqual(act("crocodile", "bite")["natural"], {"dice": "1d10", "ability": "str"})
        self.assertEqual(load("darkmantle")["speed"], {"walk": 10, "fly": 30})
        self.assertEqual(load("darkmantle")["senses"]["blindsight"], 60)
        crush = act("darkmantle", "crush")
        self.assertEqual(crush["natural"], {"dice": "1d6", "ability": "str"})
        self.assertEqual(crush["damage"], [{"dice": "1d6+3", "type": "bludgeoning"}])
        self.assertNotIn("on_hit", crush)
        self.assertNotIn("effect", trait("darkmantle", "echolocation"))
        self.assertNotIn("effect", trait("darkmantle", "false_appearance"))
        aura = act("darkmantle", "darkness_aura")
        self.assertEqual(aura["kind"], "special")
        self.assertNotIn("effect", aura)
        self.assertNotIn("usage", aura)
        self.assertNotIn("area", aura)
        self.assertEqual(set(aura), {"id", "name", "kind", "description"})
        self.assertEqual(act("rust-monster", "bite")["natural"], {"dice": "1d8", "ability": "str"})
        self.assertEqual(act("rust-monster", "bite")["ability"], "str")
        self.assertEqual(act("rust-monster", "antennae")["kind"], "special")
        self.assertNotIn("effect", act("rust-monster", "antennae"))
        self.assertEqual(set(act("rust-monster", "antennae")), {"id", "name", "kind", "description"})
        self.assertNotIn("effect", trait("rust-monster", "rust_metal"))
        self.assertNotIn("effect", trait("rust-monster", "iron_scent"))
        ooze = load("gray-ooze")
        self.assertEqual(ooze["speed"], {"walk": 10, "climb": 10})
        self.assertEqual(ooze["senses"]["blindsight"], 60)
        self.assertEqual(ooze["condition_immunities"],
                         ["blinded", "charmed", "deafened", "exhaustion", "frightened", "prone"])
        self.assertEqual(ooze["damage_resistances"], ["acid", "cold", "fire"])
        pod = act("gray-ooze", "pseudopod")
        self.assertEqual(pod["natural"], {"dice": "1d6", "ability": "str"})
        self.assertEqual(pod["damage"], [{"dice": "1d6+1", "type": "bludgeoning"}, {"dice": "2d6", "type": "acid"}])
        self.assertNotIn("on_hit", pod)
        self.assertNotIn("effect", trait("gray-ooze", "corrode_metal"))
        self.assertNotIn("effect", trait("gray-ooze", "amorphous"))
        self.assertNotIn("effect", trait("gray-ooze", "false_appearance"))
        self.assertNotIn("engulf", {a["id"] for a in ooze["actions"]})
        self.assertNotIn("transparent", {t["id"] for t in ooze["traits"]})
        gnome = load("deep-gnome")
        self.assertEqual(gnome["tags"], ["gnome"])
        self.assertEqual(gnome["senses"]["darkvision"], 120)
        self.assertEqual([e["item"] for e in gnome["equipment"]], ["srd51.item.chain_shirt", "srd51.item.war_pick"])
        self.assertNotIn("spellcasting", gnome)
        self.assertNotIn("effect", trait("deep-gnome", "stone_camouflage"))
        self.assertNotIn("effect", trait("deep-gnome", "innate_spellcasting"))
        self.assertEqual(trait("deep-gnome", "gnome_cunning")["effect"],
                         {"kind": "save_advantage", "magical": True, "abilities": ["int", "wis", "cha"]})
        self.assertEqual(act("deep-gnome", "war_pick")["weapon"], "srd51.item.war_pick")
        dart = act("deep-gnome", "poisoned_dart")
        self.assertEqual(dart["natural"], {"dice": "1d4", "ability": "dex"})
        self.assertEqual(dart["range"], [30, 120])
        self.assertNotIn("weapon", dart)
        self.assertEqual(dart["on_hit"], [{"kind": "save_condition", "save": {"ability": "con", "dc": 12},
                                           "apply": [{"condition": "poisoned", "duration": {"kind": "rounds", "n": 10},
                                                      "repeat_save": "end_of_turn"}]}])

    def test_cr1_batch_h(self):
        # CR 1 batch H. Green attacks are data. Luring Song, Fey Charm, Ink Cloud, Enlarge, and Invisibility stay
        # description-only specials: no lure, no save_effect charm, no zone, no enlarge, no invisibility.
        def trait(idx, tid):
            return {t["id"]: t for t in load(idx)["traits"]}[tid]

        def act(idx, aid):
            return {a["id"]: a for a in load(idx)["actions"]}[aid]

        def plain_special(action):
            self.assertEqual(action["kind"], "special")
            self.assertEqual(set(action), {"id", "name", "kind", "description"})

        harpy = load("harpy")
        self.assertEqual(harpy["speed"], {"walk": 20, "fly": 40})
        opts = {o["id"]: o["attacks"] for t in harpy["traits"] if t.get("effect", {}).get("kind") == "multiattack"
                for o in t["effect"]["options"]}
        self.assertEqual(opts, {"claws_club": ["claws", "club"]})
        self.assertEqual(act("harpy", "claws")["natural"], {"dice": "2d4", "ability": "str"})
        self.assertEqual(act("harpy", "claws")["ability"], "str")
        self.assertEqual(act("harpy", "club")["weapon"], "srd51.item.club")
        self.assertEqual(act("harpy", "club")["damage"], [{"dice": "1d4+1", "type": "bludgeoning"}])
        plain_special(act("harpy", "luring_song"))
        self.assertNotIn("save_effect", {a["kind"] for a in harpy["actions"]})

        dryad = load("dryad")
        self.assertEqual(dryad["armor_class"]["value"], 11)
        self.assertNotIn("natural_armor", dryad)
        self.assertEqual(act("dryad", "club")["weapon"], "srd51.item.club")
        self.assertEqual(act("dryad", "club")["ability"], "str")
        self.assertEqual((act("dryad", "club")["to_hit"], act("dryad", "club")["damage"]),
                         (2, [{"dice": "1d4", "type": "bludgeoning"}]))
        self.assertNotIn("spellcasting", dryad)
        self.assertNotIn("effect", trait("dryad", "innate_spellcasting"))
        self.assertNotIn("effect", trait("dryad", "speak_with_beasts_and_plants"))
        self.assertNotIn("effect", trait("dryad", "tree_stride"))
        self.assertEqual(trait("dryad", "magic_resistance")["effect"], {"kind": "save_advantage", "magical": True})
        plain_special(act("dryad", "fey_charm"))
        self.assertNotIn("save_effect", {a["kind"] for a in dryad["actions"]})

        due = load("duergar")
        self.assertEqual(due["tags"], ["dwarf"])
        self.assertEqual(due["senses"]["darkvision"], 120)
        self.assertEqual(due["damage_resistances"], ["poison"])
        self.assertEqual([e["item"] for e in due["equipment"]],
                         ["srd51.item.scale_mail", "srd51.item.shield", "srd51.item.war_pick", "srd51.item.javelin"])
        self.assertEqual(act("duergar", "war_pick")["damage"], [{"dice": "1d8+2", "type": "piercing"}])
        self.assertEqual(act("duergar", "javelin")["damage"], [{"dice": "1d6+2", "type": "piercing"}])
        self.assertEqual(act("duergar", "javelin_thrown")["use"], "thrown")
        self.assertEqual(act("duergar", "javelin_thrown")["range"], [30, 120])
        self.assertEqual(act("duergar", "javelin_thrown")["damage"], [{"dice": "1d6+2", "type": "piercing"}])
        for a in due["actions"]:
            for comp in a.get("damage") or []:
                self.assertNotIn(comp.get("dice", ""), ("2d8+2", "2d6+2"))
        self.assertNotIn("effect", trait("duergar", "duergar_resilience"))
        self.assertNotIn("effect", trait("duergar", "sunlight_sensitivity"))
        plain_special(act("duergar", "enlarge"))
        plain_special(act("duergar", "invisibility"))
        self.assertNotIn("spellcasting", due)

        spec = load("specter")
        self.assertEqual(spec["speed"], {"walk": 0, "fly": 50, "hover": True})
        self.assertEqual(spec["damage_resistances"], ["acid", "cold", "fire", "lightning", "thunder"])
        self.assertEqual(spec["damage_immunities"], ["necrotic", "poison"])
        self.assertEqual(spec["condition_immunities"],
                         ["charmed", "exhaustion", "grappled", "paralyzed", "petrified", "poisoned", "prone",
                          "restrained", "unconscious"])
        self.assertEqual(trait("specter", "damage_resistances_nonmagical")["effect"],
                         {"kind": "damage_mitigation", "resistances": {"nonmagical": ["bludgeoning", "piercing", "slashing"]}})
        self.assertNotIn("effect", trait("specter", "incorporeal_movement"))
        self.assertNotIn("effect", trait("specter", "sunlight_sensitivity"))
        drain = act("specter", "life_drain")
        self.assertEqual(drain["kind"], "melee_weapon_attack")
        self.assertEqual(drain["natural"], {"dice": "3d6", "ability": "dex", "damage_ability": "none"})
        self.assertEqual(drain["damage"], [{"dice": "3d6", "type": "necrotic"}])
        self.assertEqual(drain["drain"], {"max_hp": True, "save": {"ability": "con", "dc": 10}})
        self.assertNotIn("ethereal", {t["id"] for t in spec["traits"]} | {a["id"] for a in spec["actions"]})

        octo = load("giant-octopus")
        self.assertEqual(octo["speed"], {"walk": 10, "swim": 60})
        self.assertEqual(octo["skills"], {"perception": 4, "stealth": 5})
        tent = act("giant-octopus", "tentacles")
        self.assertEqual(tent["natural"], {"dice": "2d6", "ability": "str"})
        self.assertEqual((tent["to_hit"], tent["reach"]), (5, 15))
        self.assertEqual(tent["on_hit"], [{"kind": "grapple", "escape_dc": 16, "restrains": True, "exclusive": True}])
        self.assertNotIn("effect", trait("giant-octopus", "hold_breath"))
        self.assertNotIn("effect", trait("giant-octopus", "underwater_camouflage"))
        self.assertNotIn("effect", trait("giant-octopus", "water_breathing"))
        plain_special(act("giant-octopus", "ink_cloud"))
        self.assertNotIn("area", act("giant-octopus", "ink_cloud"))
        self.assertNotIn("save_effect", {a["kind"] for a in octo["actions"]})

        swarm = load("swarm-of-quippers")
        self.assertEqual((swarm["type"], swarm["tags"], swarm["speed"]),
                         ("beast", ["swarm"], {"walk": 0, "swim": 40}))
        self.assertEqual(swarm["damage_resistances"], ["bludgeoning", "piercing", "slashing"])
        self.assertEqual(swarm["condition_immunities"],
                         ["charmed", "frightened", "grappled", "paralyzed", "petrified", "prone", "restrained", "stunned"])
        bites = act("swarm-of-quippers", "bites")
        self.assertEqual(bites["natural"], {"dice": "4d6", "ability": "dex", "damage_ability": "none"})
        self.assertEqual((bites["to_hit"], bites["reach"], bites["half_hp_damage"]), (5, 0, "2d6"))
        self.assertEqual(bites["damage"], [{"dice": "4d6", "type": "piercing"}])
        self.assertNotIn("effect", trait("swarm-of-quippers", "blood_frenzy"))
        self.assertNotIn("effect", trait("swarm-of-quippers", "swarm"))
        self.assertNotIn("effect", trait("swarm-of-quippers", "water_breathing"))

    def test_cr1_batch_g(self):
        # CR 1 batch G. Pounce is the panther claw on_hit (Str DC 13, moved 20, prone untimed). The bonus-action
        # bite vs the prone target stays in the trait text (no prone-gated bonus_attack). Flyby is not on these
        # SRD blocks, so it is not a trait. Giant eagle is the only Str/Dex tie (both +3).
        def trait(idx, tid):
            return {t["id"]: t for t in load(idx)["traits"]}[tid]

        def act(idx, aid):
            return {a["id"]: a for a in load(idx)["actions"]}[aid]

        pounce = [{"kind": "save_condition", "save": {"ability": "str", "dc": 13}, "apply": [{"condition": "prone"}],
                   "when": [{"kind": "moved_at_least_ft", "ft": 20}]}]
        for idx, bite_dice, claw_dice in (("lion", "1d8", "1d6"), ("tiger", "1d10", "1d8")):
            self.assertEqual(act(idx, "bite")["natural"], {"dice": bite_dice, "ability": "str"})
            self.assertEqual(act(idx, "claw")["natural"], {"dice": claw_dice, "ability": "str"})
            self.assertEqual((act(idx, "bite")["to_hit"], act(idx, "claw")["to_hit"]), (5, 5))
            self.assertNotIn("ability", act(idx, "bite"))
            self.assertNotIn("ability", act(idx, "claw"))
            self.assertNotIn("on_hit", act(idx, "bite"))
            self.assertEqual(act(idx, "claw")["on_hit"], pounce)
            self.assertNotIn("effect", trait(idx, "pounce"))
            self.assertNotIn("effect", trait(idx, "keen_smell"))
            self.assertIn("bonus action", trait(idx, "pounce")["description"])
            self.assertNotIn("bonus_attack", str(load(idx)))
            self.assertNotIn("bonus_actions", load(idx))
            self.assertEqual(load(idx)["skills"], {"perception": 3, "stealth": 6})
        self.assertEqual([t["effect"] for t in load("lion")["traits"] if "effect" in t], [{"kind": "pack_tactics"}])
        self.assertNotIn("effect", trait("lion", "running_leap"))
        self.assertNotIn("darkvision", load("lion")["senses"])
        self.assertNotIn("pack_tactics", {t["id"] for t in load("tiger")["traits"]})
        self.assertEqual(load("tiger")["senses"]["darkvision"], 60)
        self.assertNotIn("flyby", {t["id"] for t in load("lion")["traits"]})
        self.assertNotIn("flyby", {t["id"] for t in load("tiger")["traits"]})
        for idx in ("giant-eagle", "giant-vulture", "hippogriff"):
            ids = {t["id"] for t in load(idx)["traits"]}
            self.assertNotIn("flyby", ids)
            self.assertNotIn("effect", trait(idx, "keen_sight" if idx != "giant-vulture" else "keen_sight_and_smell"))
        self.assertEqual(load("giant-eagle")["speed"], {"walk": 10, "fly": 80})
        self.assertEqual(load("giant-eagle")["languages"],
                         ["Giant Eagle", "understands Common and Auran but can't speak"])
        self.assertEqual(act("giant-eagle", "beak")["ability"], "str")
        self.assertEqual(act("giant-eagle", "talons")["ability"], "str")
        self.assertEqual(act("giant-eagle", "beak")["natural"], {"dice": "1d6", "ability": "str"})
        self.assertEqual(act("giant-eagle", "talons")["natural"], {"dice": "2d6", "ability": "str"})
        self.assertEqual(load("giant-vulture")["speed"], {"walk": 10, "fly": 60})
        self.assertEqual(load("giant-vulture")["languages"], ["understands Common but can't speak"])
        self.assertEqual(trait("giant-vulture", "pack_tactics")["effect"], {"kind": "pack_tactics"})
        self.assertNotIn("ability", act("giant-vulture", "beak"))
        self.assertNotIn("ability", act("giant-vulture", "talons"))
        self.assertEqual(act("giant-vulture", "beak")["damage"], [{"dice": "2d4+2", "type": "piercing"}])
        self.assertEqual(act("giant-vulture", "talons")["damage"], [{"dice": "2d6+2", "type": "slashing"}])
        self.assertEqual(load("hippogriff")["speed"], {"walk": 40, "fly": 60})
        self.assertEqual(load("hippogriff")["type"], "monstrosity")
        self.assertEqual(load("hippogriff")["skills"], {"perception": 5})
        self.assertEqual(load("hippogriff")["languages"], [])
        self.assertNotIn("ability", act("hippogriff", "beak"))
        self.assertNotIn("ability", act("hippogriff", "claws"))
        self.assertEqual(act("hippogriff", "beak")["natural"], {"dice": "1d10", "ability": "str"})
        self.assertEqual(act("hippogriff", "claws")["natural"], {"dice": "2d6", "ability": "str"})
        self.assertEqual(len(trait("giant-eagle", "multiattack")["effect"]["options"]), 1)
        self.assertEqual(len(trait("giant-vulture", "multiattack")["effect"]["options"]), 1)
        self.assertEqual(len(trait("hippogriff", "multiattack")["effect"]["options"]), 1)

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

    def test_cr2_batch_j(self):
        # CR 2 batch J. Centaur Charge is pike on_hit only (extra 3d6 piercing after 30 ft; no prone, no melee
        # on_event). Minotaur Skeleton Charge is gore on_hit (extra 2d8 piercing, Str DC 14 prone); the 10 ft push
        # stays in the trait text. Ankheg Acid Spray is the wyrmling line. Mimic stays deferred.
        def trait(idx, tid):
            return {t["id"]: t for t in load(idx)["traits"]}[tid]

        def act(idx, aid):
            return {a["id"]: a for a in load(idx)["actions"]}[aid]

        cen = load("centaur")
        self.assertEqual(cen["speed"], {"walk": 50})
        self.assertEqual([e["item"] for e in cen["equipment"]], ["srd51.item.pike", "srd51.item.longbow"])
        self.assertEqual(cen["hands"], 2)
        self.assertNotIn("effect", trait("centaur", "charge"))
        self.assertNotIn("on_event", json.dumps(trait("centaur", "charge")))
        pike = act("centaur", "pike")
        self.assertEqual(pike["weapon"], "srd51.item.pike")
        self.assertEqual(pike["use"], "two_hands")
        self.assertEqual((pike["to_hit"], pike["reach"]), (6, 10))
        self.assertEqual(pike["damage"], [{"dice": "1d10+4", "type": "piercing"}])
        self.assertEqual(pike["on_hit"], [{"kind": "extra_damage", "damage": [{"dice": "3d6", "type": "piercing"}],
                                           "when": [{"kind": "moved_at_least_ft", "ft": 30}]}])
        self.assertNotIn("save_condition", json.dumps(pike))
        self.assertNotIn("prone", json.dumps(pike))
        self.assertNotIn("on_hit", act("centaur", "hooves"))
        self.assertEqual(act("centaur", "hooves")["natural"], {"dice": "2d6", "ability": "str"})
        bow = act("centaur", "longbow")
        self.assertEqual(bow["weapon"], "srd51.item.longbow")
        self.assertEqual((bow["to_hit"], bow["range"]), (4, [150, 600]))
        self.assertNotIn("natural", bow)

        gr = load("griffon")
        self.assertEqual(gr["speed"], {"walk": 30, "fly": 80})
        self.assertEqual(gr["skills"], {"perception": 5})
        self.assertEqual(gr["senses"]["darkvision"], 60)
        self.assertNotIn("effect", trait("griffon", "keen_sight"))
        self.assertEqual(act("griffon", "beak")["natural"], {"dice": "1d8", "ability": "str"})
        self.assertEqual(act("griffon", "claws")["natural"], {"dice": "2d6", "ability": "str"})
        self.assertNotIn("ability", act("griffon", "beak"))
        self.assertNotIn("ability", act("griffon", "claws"))
        self.assertEqual(len(trait("griffon", "multiattack")["effect"]["options"]), 1)

        peg = load("pegasus")
        self.assertEqual((peg["type"], peg["speed"], peg["hands"]),
                         ("celestial", {"walk": 60, "fly": 90}, 0))
        self.assertEqual(peg["saving_throws"], {"dex": 4, "wis": 4, "cha": 3})
        self.assertEqual(peg["skills"], {"perception": 6})
        self.assertEqual(act("pegasus", "hooves")["natural"], {"dice": "2d6", "ability": "str"})
        self.assertNotIn("traits", peg)
        self.assertNotIn("weapon", act("pegasus", "hooves"))

        ank = load("ankheg")
        self.assertEqual(ank["speed"], {"walk": 30, "burrow": 10})
        self.assertEqual(ank["natural_armor"], {"base": 14, "dex": True})
        self.assertEqual(ank["senses"]["darkvision"], 60)
        self.assertEqual(ank["senses"]["tremorsense"], 60)
        bite = act("ankheg", "bite")
        self.assertEqual(bite["natural"], {"dice": "2d6", "ability": "str"})
        self.assertEqual(bite["damage"], [{"dice": "2d6+3", "type": "slashing"}, {"dice": "1d6", "type": "acid"}])
        self.assertEqual(bite["on_hit"], [{"kind": "grapple", "escape_dc": 13, "exclusive": True}])
        self.assertNotIn("restrains", bite["on_hit"][0])
        self.assertIn("advantage", bite["description"])
        self.assertIn("Large or smaller", bite["description"])
        spray = act("ankheg", "acid_spray")
        self.assertEqual(spray["kind"], "save_effect")
        self.assertEqual(spray["save"], {"ability": "dex", "dc": 13})
        self.assertEqual(spray["area"], {"shape": "line", "size_ft": 30, "width_ft": 5})
        self.assertEqual(spray["damage"], [{"dice": "3d6", "type": "acid"}])
        self.assertEqual((spray["on_success"], spray["usage"]), ("half", {"kind": "recharge", "min": 6}))
        self.assertIn("no creature grappled", spray["description"])
        self.assertNotIn("when", spray)
        self.assertIn("11 while prone", ank["armor_class"]["note"])

        snake = load("giant-constrictor-snake")
        self.assertEqual((snake["size"], snake["speed"], snake["hands"]),
                         ("huge", {"walk": 30, "swim": 30}, 0))
        self.assertEqual(snake["senses"]["blindsight"], 10)
        self.assertEqual(act("giant-constrictor-snake", "bite")["reach"], 10)
        self.assertNotIn("on_hit", act("giant-constrictor-snake", "bite"))
        con = act("giant-constrictor-snake", "constrict")
        self.assertEqual(con["natural"], {"dice": "2d8", "ability": "str"})
        self.assertEqual(con["on_hit"], [{"kind": "grapple", "escape_dc": 16, "restrains": True, "exclusive": True}])

        sk = load("minotaur-skeleton")
        self.assertEqual(sk["damage_vulnerabilities"], ["bludgeoning"])
        self.assertEqual(sk["damage_immunities"], ["poison"])
        self.assertEqual(sk["condition_immunities"], ["poisoned", "exhaustion"])
        self.assertEqual(sk["hands"], 2)
        self.assertEqual(sk["weapon_dice_multiplier"], 2)
        self.assertEqual([e["item"] for e in sk["equipment"]], ["srd51.item.greataxe"])
        axe = act("minotaur-skeleton", "greataxe")
        self.assertEqual(axe["weapon"], "srd51.item.greataxe")
        self.assertEqual(axe["use"], "two_hands")
        self.assertNotIn("natural", axe)
        self.assertEqual(axe["damage"], [{"dice": "2d12+4", "type": "slashing"}])
        self.assertNotIn("effect", trait("minotaur-skeleton", "charge"))
        self.assertIn("pushed up to 10 feet", trait("minotaur-skeleton", "charge")["description"])
        gore = act("minotaur-skeleton", "gore")
        self.assertEqual(gore["natural"], {"dice": "2d8", "ability": "str"})
        self.assertEqual(gore["on_hit"], [
            {"kind": "extra_damage", "damage": [{"dice": "2d8", "type": "piercing"}],
             "when": [{"kind": "moved_at_least_ft", "ft": 10}]},
            {"kind": "save_condition", "save": {"ability": "str", "dc": 14},
             "apply": [{"condition": "prone"}], "when": [{"kind": "moved_at_least_ft", "ft": 10}]},
        ])
        self.assertNotIn("forced_move", json.dumps(sk))

    def test_cr2_batch_i(self):
        # CR 2 batch I. Polar bear is the brown-bear multiattack (printed +7 derives; Keen Smell descriptive;
        # Cave Bear is an alias, not a file). Giant boar and rhinoceros are the boar/goat trait-level Charge.
        # Giant elk is the elk ram on_hit (not a trait on_event, so hooves do not charge). Saber-toothed tiger
        # is the tiger claw Pounce; printed damage +5 is a damage_override over Str 18. Hunter shark is the
        # quipper Blood Frenzy partial with reef-shark swim-only speed and no Pack Tactics.

        def trait(idx, tid):
            return {t["id"]: t for t in load(idx)["traits"]}[tid]

        def act(idx, aid):
            return {a["id"]: a for a in load(idx)["actions"]}[aid]

        when = [{"kind": "weapon", "is": "melee"}, {"kind": "moved_at_least_ft", "ft": 20}]
        moved = [{"kind": "moved_at_least_ft", "ft": 20}]

        bear = load("polar-bear")
        self.assertEqual(bear["speed"], {"walk": 40, "swim": 30})
        self.assertEqual(bear["skills"], {"perception": 3})
        self.assertEqual(bear["natural_armor"], {"base": 12, "dex": True})
        self.assertEqual((act("polar-bear", "bite")["to_hit"], act("polar-bear", "claws")["to_hit"]), (7, 7))
        self.assertEqual(act("polar-bear", "bite")["natural"], {"dice": "1d8", "ability": "str"})
        self.assertEqual(act("polar-bear", "claws")["natural"], {"dice": "2d6", "ability": "str"})
        self.assertEqual(act("polar-bear", "bite")["damage"], [{"dice": "1d8+5", "type": "piercing"}])
        self.assertEqual(act("polar-bear", "claws")["damage"], [{"dice": "2d6+5", "type": "slashing"}])
        self.assertNotIn("attack_bonus", act("polar-bear", "bite"))
        self.assertNotIn("attack_bonus", act("polar-bear", "claws"))
        self.assertNotIn("effect", trait("polar-bear", "keen_smell"))
        self.assertEqual([t["id"] for t in bear["traits"] if "effect" in t], ["multiattack"])
        self.assertFalse((PACK / "monsters" / "cave_bear.json").exists())

        boar = trait("giant-boar", "charge")["effect"]
        self.assertEqual((boar["kind"], boar["on"], boar["limit"], boar["when"]), ("on_event", "hit", "once_per_turn", when))
        self.assertEqual(boar["effects"], [
            {"kind": "extra_damage", "damage": [{"dice": "2d6", "type": "slashing"}]},
            {"kind": "save_condition", "save": {"ability": "str", "dc": 13}, "apply": [{"condition": "prone"}]}])
        rel = trait("giant-boar", "relentless")["effect"]
        self.assertEqual((rel["on"], rel["when"]), ("drop_to_zero", [{"kind": "damage_at_most", "amount": 10}]))
        self.assertEqual((rel["usage"], rel["effects"]),
                         ({"kind": "per_rest", "uses": 1, "rest": "short"}, [{"kind": "set_hp", "value": 1}]))
        self.assertIn("Recharges", trait("giant-boar", "relentless")["name"])
        self.assertEqual(act("giant-boar", "tusk")["natural"], {"dice": "2d6", "ability": "str"})
        self.assertEqual((act("giant-boar", "tusk")["to_hit"], act("giant-boar", "tusk")["damage"]),
                         (5, [{"dice": "2d6+3", "type": "slashing"}]))
        self.assertEqual(load("giant-boar")["natural_armor"], {"base": 12, "dex": True})
        self.assertEqual(load("giant-boar")["speed"], {"walk": 40})

        self.assertNotIn("effect", trait("giant-elk", "charge"))
        ram = act("giant-elk", "ram")
        self.assertEqual(ram["natural"], {"dice": "2d6", "ability": "str"})
        self.assertEqual((ram["to_hit"], ram["reach"]), (6, 10))
        self.assertEqual(ram["on_hit"], [
            {"kind": "extra_damage", "damage": [{"dice": "2d6", "type": "bludgeoning"}], "when": moved},
            {"kind": "save_condition", "save": {"ability": "str", "dc": 14}, "apply": [{"condition": "prone"}], "when": moved}])
        hooves = act("giant-elk", "hooves")
        self.assertEqual(hooves["natural"], {"dice": "4d8", "ability": "str"})
        self.assertEqual((hooves["to_hit"], hooves["reach"]), (6, 5))
        self.assertNotIn("on_hit", hooves)
        self.assertIn("one prone creature", hooves["description"])
        self.assertEqual(load("giant-elk")["speed"], {"walk": 60})
        self.assertEqual(load("giant-elk")["skills"], {"perception": 4})
        self.assertEqual(load("giant-elk")["natural_armor"], {"base": 11, "dex": True})
        self.assertEqual(load("giant-elk")["languages"],
                         ["Giant Elk", "understands Common, Elvish, and Sylvan but can't speak"])

        rhino = trait("rhinoceros", "charge")["effect"]
        self.assertEqual((rhino["kind"], rhino["on"], rhino["limit"], rhino["when"]), ("on_event", "hit", "once_per_turn", when))
        self.assertEqual(rhino["effects"], [
            {"kind": "extra_damage", "damage": [{"dice": "2d8", "type": "bludgeoning"}]},
            {"kind": "save_condition", "save": {"ability": "str", "dc": 15}, "apply": [{"condition": "prone"}]}])
        self.assertEqual(act("rhinoceros", "gore")["natural"], {"dice": "2d8", "ability": "str"})
        self.assertEqual((act("rhinoceros", "gore")["to_hit"], act("rhinoceros", "gore")["damage"]),
                         (7, [{"dice": "2d8+5", "type": "bludgeoning"}]))
        self.assertEqual(load("rhinoceros")["natural_armor"], {"base": 12, "dex": True})
        self.assertEqual(load("rhinoceros")["armor_class"], {"value": 11, "note": "natural armor"})

        pounce = [{"kind": "save_condition", "save": {"ability": "str", "dc": 14}, "apply": [{"condition": "prone"}],
                   "when": moved}]
        tiger = load("saber-toothed-tiger")
        self.assertEqual(tiger["skills"], {"perception": 3, "stealth": 6})
        self.assertNotIn("darkvision", tiger["senses"])
        self.assertNotIn("natural_armor", tiger)
        self.assertEqual(act("saber-toothed-tiger", "bite")["natural"], {"dice": "1d10", "ability": "str"})
        self.assertEqual(act("saber-toothed-tiger", "claw")["natural"], {"dice": "2d6", "ability": "str"})
        self.assertEqual((act("saber-toothed-tiger", "bite")["to_hit"], act("saber-toothed-tiger", "claw")["to_hit"]), (6, 6))
        self.assertNotIn("attack_bonus", act("saber-toothed-tiger", "bite"))
        self.assertNotIn("attack_bonus", act("saber-toothed-tiger", "claw"))
        self.assertEqual(act("saber-toothed-tiger", "bite")["damage_override"]["dice"], "1d10+5")
        self.assertEqual(act("saber-toothed-tiger", "claw")["damage_override"]["dice"], "2d6+5")
        self.assertEqual(act("saber-toothed-tiger", "bite")["damage"], [{"dice": "1d10+5", "type": "piercing"}])
        self.assertEqual(act("saber-toothed-tiger", "claw")["damage"], [{"dice": "2d6+5", "type": "slashing"}])
        self.assertNotIn("on_hit", act("saber-toothed-tiger", "bite"))
        self.assertEqual(act("saber-toothed-tiger", "claw")["on_hit"], pounce)
        self.assertNotIn("effect", trait("saber-toothed-tiger", "pounce"))
        self.assertNotIn("effect", trait("saber-toothed-tiger", "keen_smell"))
        self.assertIn("bonus action", trait("saber-toothed-tiger", "pounce")["description"])
        self.assertNotIn("bonus_attack", str(tiger))
        self.assertNotIn("bonus_actions", tiger)
        self.assertNotIn("pack_tactics", {t["id"] for t in tiger["traits"]})

        shark = load("hunter-shark")
        self.assertEqual(shark["speed"], {"walk": 0, "swim": 40})
        self.assertEqual(shark["senses"]["darkvision"], 30)
        self.assertEqual(shark["skills"], {"perception": 2})
        self.assertEqual(shark["natural_armor"], {"base": 11, "dex": True})
        self.assertEqual(act("hunter-shark", "bite")["natural"], {"dice": "2d8", "ability": "str"})
        self.assertEqual((act("hunter-shark", "bite")["to_hit"], act("hunter-shark", "bite")["damage"]),
                         (6, [{"dice": "2d8+4", "type": "piercing"}]))
        self.assertNotIn("effect", trait("hunter-shark", "blood_frenzy"))
        self.assertNotIn("effect", trait("hunter-shark", "water_breathing"))
        self.assertNotIn("pack_tactics", {t["id"] for t in shark["traits"]})
        self.assertFalse((PACK / "monsters" / "mimic.json").exists())

    def test_cr2_batch_k(self):
        # CR 2 batch K. Druid spellcasting lists only pack-present spells; the trait text keeps the four
        # missing names, and barkskin / shillelagh stay off the numbers (as dryad). Priest Divine Eminence
        # is text only. Merrow's harpoon is natural (no item) and the pull stays text. Wererat is the
        # humanoid form; shapechange, bite and the curse stay text. Sea hag is parked. Mimic stays deferred.

        def trait(idx, tid):
            return {t["id"]: t for t in load(idx)["traits"]}[tid]

        def act(idx, aid):
            return {a["id"]: a for a in load(idx)["actions"]}[aid]

        def opts(idx):
            return {o["id"]: o["attacks"] for t in load(idx)["traits"] if t.get("effect", {}).get("kind") == "multiattack"
                    for o in t["effect"]["options"]}

        druid = load("druid")
        self.assertEqual(druid["armor_class"], {"value": 11})
        self.assertNotIn("natural_armor", druid)
        self.assertEqual(druid["skills"], {"medicine": 4, "nature": 3, "perception": 4})
        self.assertEqual(druid["spellcasting"]["ability"], "wis")
        self.assertEqual(druid["spellcasting"]["caster_level"], 4)
        self.assertEqual(druid["spellcasting"]["slots"], {"1": 4, "2": 3})
        self.assertEqual((druid["spellcasting"]["dc"], druid["spellcasting"]["attack_bonus"]), (12, 4))
        self.assertEqual(druid["spellcasting"]["spells"], [
            "srd51.spell.produce_flame", "srd51.spell.shillelagh", "srd51.spell.entangle",
            "srd51.spell.thunderwave", "srd51.spell.barkskin"])
        spell_text = trait("druid", "spellcasting")["description"]
        for missing in ("druidcraft", "longstrider", "speak with animals", "animal messenger"):
            self.assertIn(missing, spell_text)
        self.assertNotIn("effect", trait("druid", "spellcasting"))
        self.assertNotIn("srd51.spell.druidcraft", json.dumps(druid))
        staff = act("druid", "quarterstaff")
        staff2 = act("druid", "quarterstaff_two_handed")
        self.assertEqual(staff["weapon"], "srd51.item.quarterstaff")
        self.assertEqual(staff2["use"], "two_hands")
        self.assertEqual((staff["to_hit"], staff["damage"]), (2, [{"dice": "1d6", "type": "bludgeoning"}]))
        self.assertEqual((staff2["to_hit"], staff2["damage"]), (2, [{"dice": "1d8", "type": "bludgeoning"}]))
        self.assertIn("shillelagh", staff["description"])
        self.assertNotIn("1d8+2", json.dumps(staff["damage"]))
        self.assertFalse((PACK / "spells" / "druidcraft.json").exists())

        priest = load("priest")
        self.assertEqual(priest["armor_class"], {"value": 13, "note": "chain shirt"})
        self.assertEqual(priest["speed"], {"walk": 25})
        self.assertEqual([e["item"] for e in priest["equipment"]], ["srd51.item.chain_shirt", "srd51.item.mace"])
        self.assertEqual(priest["skills"], {"medicine": 7, "persuasion": 3, "religion": 4})
        self.assertEqual(priest["spellcasting"]["caster_level"], 5)
        self.assertEqual(priest["spellcasting"]["slots"], {"1": 4, "2": 3, "3": 2})
        self.assertEqual((priest["spellcasting"]["dc"], priest["spellcasting"]["attack_bonus"]), (13, 5))
        self.assertEqual(priest["spellcasting"]["spells"], [
            "srd51.spell.light", "srd51.spell.sacred_flame", "srd51.spell.thaumaturgy",
            "srd51.spell.cure_wounds", "srd51.spell.guiding_bolt", "srd51.spell.sanctuary",
            "srd51.spell.lesser_restoration", "srd51.spell.spiritual_weapon",
            "srd51.spell.dispel_magic", "srd51.spell.spirit_guardians"])
        self.assertNotIn("effect", trait("priest", "divine_eminence"))
        self.assertNotIn("bonus_actions", priest)
        self.assertIn("spell slot", trait("priest", "divine_eminence")["description"])
        mace = act("priest", "mace")
        self.assertEqual(mace["weapon"], "srd51.item.mace")
        self.assertEqual(mace["ability"], "str")
        self.assertEqual((mace["to_hit"], mace["damage"]), (2, [{"dice": "1d6", "type": "bludgeoning"}]))

        merrow = load("merrow")
        self.assertEqual((merrow["size"], merrow["type"], merrow["hands"]), ("large", "monstrosity", 0))
        self.assertEqual(merrow["natural_armor"], {"base": 13, "dex": True})
        self.assertEqual(merrow["speed"], {"walk": 10, "swim": 40})
        self.assertEqual(merrow["senses"]["darkvision"], 60)
        self.assertNotIn("effect", trait("merrow", "amphibious"))
        self.assertEqual(opts("merrow"), {
            "bite_claws": ["bite", "claws"],
            "bite_harpoon": ["bite", "harpoon"],
            "bite_harpoon_thrown": ["bite", "harpoon_thrown"],
        })
        self.assertEqual(act("merrow", "bite")["natural"], {"dice": "1d8", "ability": "str"})
        self.assertEqual(act("merrow", "claws")["natural"], {"dice": "2d4", "ability": "str"})
        self.assertEqual(act("merrow", "harpoon")["natural"], {"dice": "2d6", "ability": "str"})
        thrown = act("merrow", "harpoon_thrown")
        self.assertEqual(thrown["natural"], {"dice": "2d6", "ability": "str"})
        self.assertEqual(thrown["range"], [20, 60])
        self.assertNotIn("weapon", thrown)
        self.assertNotIn("use", thrown)
        self.assertIn("pulled up to 20 feet", thrown["description"])
        self.assertNotIn("forced_move", json.dumps(merrow))
        self.assertFalse((PACK / "items" / "harpoon.json").exists())

        rat = load("wererat")
        self.assertIs(rat["shapechanger"], True)
        self.assertEqual((rat["armor_class"]["value"], rat["speed"]), (12, {"walk": 30}))
        self.assertEqual(rat["skills"], {"perception": 2, "stealth": 4})
        self.assertNotIn("darkvision", rat["senses"])
        self.assertNotIn("damage_immunities", rat)
        self.assertEqual(trait("wererat", "damage_immunities_nonmagical")["effect"], {
            "kind": "damage_mitigation",
            "immunities": {"nonmagical_not_silvered": ["bludgeoning", "piercing", "slashing"]},
        })
        self.assertNotIn("effect", trait("wererat", "shapechanger"))
        self.assertNotIn("forms", trait("wererat", "shapechanger"))
        self.assertEqual(opts("wererat"), {
            "shortsword_x2": ["shortsword", "shortsword"],
            "hand_crossbow_x2": ["hand_crossbow", "hand_crossbow"],
            "shortsword_hand_crossbow": ["shortsword", "hand_crossbow"],
        })
        self.assertEqual(act("wererat", "bite")["kind"], "special")
        self.assertNotIn("natural", act("wererat", "bite"))
        self.assertIn("lycanthropy", act("wererat", "bite")["description"])
        sword = act("wererat", "shortsword")
        bow = act("wererat", "hand_crossbow")
        self.assertEqual(sword["weapon"], "srd51.item.shortsword")
        self.assertEqual(bow["weapon"], "srd51.item.crossbow_hand")
        self.assertEqual((sword["to_hit"], sword["damage"]), (4, [{"dice": "1d6+2", "type": "piercing"}]))
        self.assertEqual((bow["to_hit"], bow["range"], bow["damage"]),
                         (4, [30, 120], [{"dice": "1d6+2", "type": "piercing"}]))
        self.assertFalse((PACK / "monsters" / "sea_hag.json").exists())
        self.assertFalse((PACK / "monsters" / "mimic.json").exists())

    def test_cr2_batch_l(self):
        # CR 2 batch L. Grick's multiattack is unconditional; the hit-gate stays text. Swarm poison
        # rides the same Bites as half_hp_damage. Blinding Spittle is a point sphere, recharge 5.
        # Will-o'-wisp's other actions stay text. Ochre Jelly Split is a trait, not a spawn.
        # Sea hag, mimic, doppelganger and green hag stay deferred.

        def trait(idx, tid):
            return {t["id"]: t for t in load(idx)["traits"]}[tid]

        def act(idx, aid):
            return {a["id"]: a for a in load(idx)["actions"]}[aid]

        def opts(idx):
            return {o["id"]: o["attacks"] for t in load(idx)["traits"] if t.get("effect", {}).get("kind") == "multiattack"
                    for o in t["effect"]["options"]}

        grick = load("grick")
        self.assertEqual(grick["natural_armor"], {"base": 12, "dex": True})
        self.assertEqual(grick["speed"], {"walk": 30, "climb": 30})
        self.assertNotIn("damage_resistances", grick)
        self.assertEqual(trait("grick", "damage_resistances_nonmagical")["effect"], {
            "kind": "damage_mitigation",
            "resistances": {"nonmagical": ["bludgeoning", "piercing", "slashing"]},
        })
        self.assertNotIn("effect", trait("grick", "stone_camouflage"))
        self.assertEqual(opts("grick"), {"tentacles_beak": ["tentacles", "beak"]})
        self.assertIn("if that attack hits", trait("grick", "multiattack")["description"])
        self.assertNotIn("when", json.dumps(trait("grick", "multiattack")["effect"]))
        self.assertEqual(act("grick", "tentacles")["natural"], {"dice": "2d6", "ability": "str"})
        self.assertEqual(act("grick", "beak")["natural"], {"dice": "1d6", "ability": "str"})
        self.assertEqual((act("grick", "beak")["to_hit"], act("grick", "beak")["damage"]),
                         (4, [{"dice": "1d6+2", "type": "piercing"}]))

        snakes = load("swarm-of-poisonous-snakes")
        self.assertEqual((snakes["type"], snakes["tags"]), ("beast", ["swarm"]))
        self.assertEqual(snakes["speed"], {"walk": 30, "swim": 30})
        self.assertEqual(snakes["damage_resistances"], ["bludgeoning", "piercing", "slashing"])
        self.assertEqual(len(snakes["condition_immunities"]), 8)
        bites = act("swarm-of-poisonous-snakes", "bites")
        self.assertEqual(bites["natural"], {"dice": "2d6", "ability": "dex", "damage_ability": "none"})
        self.assertEqual(bites["reach"], 0)
        self.assertEqual(bites["half_hp_damage"], "1d6")
        self.assertEqual(bites["on_hit"], [{
            "kind": "save_condition",
            "save": {"ability": "con", "dc": 10},
            "damage": [{"dice": "4d6", "type": "poison"}],
            "on_success": "half",
        }])
        self.assertNotIn("zero_hp", bites["on_hit"][0])

        mouther = load("gibbering-mouther")
        self.assertEqual((mouther["type"], mouther["armor_class"]), ("aberration", {"value": 9}))
        self.assertEqual(mouther["speed"], {"walk": 10, "swim": 10})
        self.assertEqual(mouther["condition_immunities"], ["prone"])
        self.assertNotIn("effect", trait("gibbering-mouther", "aberrant_ground"))
        self.assertNotIn("effect", trait("gibbering-mouther", "gibbering"))
        self.assertNotIn("effect", trait("gibbering-mouther", "multiattack"))
        self.assertIn("Blinding Spittle", trait("gibbering-mouther", "multiattack")["description"])
        mb = act("gibbering-mouther", "bites")
        self.assertEqual(mb["natural"], {"dice": "5d6", "ability": "str"})
        self.assertEqual(mb["on_hit"][0]["apply"], [{"condition": "prone", "max_size": "medium"}])
        self.assertIn("absorbed", mb["description"])
        spit = act("gibbering-mouther", "blinding_spittle")
        self.assertEqual(spit["kind"], "save_effect")
        self.assertEqual(spit["origin"], "point")
        self.assertEqual(spit["range_ft"], 15)
        self.assertEqual(spit["area"], {"shape": "sphere", "size_ft": 5})
        self.assertEqual(spit["save"], {"ability": "dex", "dc": 13})
        self.assertEqual(spit["apply"], [{
            "condition": "blinded",
            "duration": {"kind": "until_end_of_caster_next_turn"},
        }])
        self.assertEqual(spit["usage"], {"kind": "recharge", "min": 5})
        self.assertNotIn("cone", json.dumps(spit["area"]))

        wisp = load("will-o-wisp")
        self.assertEqual(wisp["speed"], {"walk": 0, "fly": 50, "hover": True})
        self.assertEqual(wisp["armor_class"], {"value": 19})
        self.assertEqual(wisp["damage_resistances"], ["acid", "cold", "fire", "necrotic", "thunder"])
        self.assertEqual(wisp["damage_immunities"], ["lightning", "poison"])
        self.assertEqual(trait("will-o-wisp", "damage_resistances_nonmagical")["effect"], {
            "kind": "damage_mitigation",
            "resistances": {"nonmagical": ["bludgeoning", "piercing", "slashing"]},
        })
        for tid in ("consume_life", "ephemeral", "incorporeal_movement", "variable_illumination"):
            self.assertNotIn("effect", trait("will-o-wisp", tid))
        self.assertNotIn("bonus_actions", wisp)
        self.assertNotIn("emits_light", wisp)
        shock = act("will-o-wisp", "shock")
        self.assertEqual(shock["kind"], "melee_weapon_attack")
        self.assertEqual(shock["natural"], {"dice": "2d8", "ability": "dex", "damage_ability": "none"})
        self.assertEqual(shock["attack_bonus"]["value"], 4)
        self.assertEqual((shock["to_hit"], shock["damage"]), (4, [{"dice": "2d8", "type": "lightning"}]))
        self.assertIn("Melee Spell Attack", shock["description"])
        self.assertEqual(act("will-o-wisp", "invisibility")["kind"], "special")
        self.assertNotIn("effect", act("will-o-wisp", "invisibility"))

        jelly = load("ochre-jelly")
        self.assertEqual((jelly["size"], jelly["type"]), ("large", "ooze"))
        self.assertEqual(jelly["damage_resistances"], ["acid"])
        self.assertEqual(jelly["damage_immunities"], ["lightning", "slashing"])
        self.assertEqual(jelly["condition_immunities"],
                         ["blinded", "charmed", "deafened", "exhaustion", "frightened", "prone"])
        self.assertEqual(jelly["senses"]["blindsight"], 60)
        self.assertNotIn("effect", trait("ochre-jelly", "amorphous"))
        self.assertNotIn("effect", trait("ochre-jelly", "spider_climb"))
        self.assertNotIn("effect", trait("ochre-jelly", "split"))
        self.assertIn("no creature-spawn", trait("ochre-jelly", "split")["description"])
        self.assertNotIn("reactions", jelly)
        pod = act("ochre-jelly", "pseudopod")
        self.assertEqual(pod["natural"], {"dice": "2d6", "ability": "str"})
        self.assertEqual(pod["damage"], [
            {"dice": "2d6+2", "type": "bludgeoning"},
            {"dice": "1d6", "type": "acid"},
        ])
        self.assertFalse((PACK / "monsters" / "sea_hag.json").exists())
        self.assertFalse((PACK / "monsters" / "mimic.json").exists())
        self.assertFalse((PACK / "monsters" / "doppelganger.json").exists())
        self.assertFalse((PACK / "monsters" / "green_hag.json").exists())

    def test_cr3_batch_m(self):
        # CR 3 batch M. Giant scorpion is the giant-crab grapple (max_grapples 2, no restrain)
        # plus the scorpion / giant-spider poison rider (no zero_hp), with claw repeated in Multiattack.
        # Minotaur has no Multiattack: one greataxe or one gore. Charge copies the minotaur-skeleton
        # gore on_hit; the push stays text. Reckless is the berserker reckless_attack.
        # Hell hound and winter wolf breaths are the mephit cone with the wyrmling recharge (min 5).
        # Printed Perception is expertise (Wis mod + 2*PB), so it is not a PRINTED_SKILL_TOTALS entry
        # (unlike priest Religion). Sea hag, mimic, doppelganger and green hag stay deferred.

        def trait(idx, tid):
            return {t["id"]: t for t in load(idx)["traits"]}[tid]

        def act(idx, aid):
            return {a["id"]: a for a in load(idx)["actions"]}[aid]

        def opts(idx):
            return {o["id"]: o["attacks"] for t in load(idx)["traits"] if t.get("effect", {}).get("kind") == "multiattack"
                    for o in t["effect"]["options"]}

        moved = [{"kind": "moved_at_least_ft", "ft": 10}]

        scorp = load("giant-scorpion")
        self.assertEqual((scorp["size"], scorp["type"], scorp["hands"]), ("large", "beast", 0))
        self.assertEqual(scorp["armor_class"]["value"], 15)
        self.assertEqual(scorp["natural_armor"], {"base": 14, "dex": True})
        self.assertEqual(scorp["hit_points"], {"average": 52, "formula": "7d10+14"})
        self.assertEqual(scorp["speed"], {"walk": 40})
        self.assertEqual(scorp["senses"], {"blindsight": 60, "passive_perception": 9})
        self.assertNotIn("skills", scorp)
        self.assertEqual(opts("giant-scorpion"), {"claws_sting": ["claw", "claw", "sting"]})
        claw = act("giant-scorpion", "claw")
        self.assertEqual(claw["natural"], {"dice": "1d8", "ability": "str"})
        self.assertEqual((claw["to_hit"], claw["damage"]), (4, [{"dice": "1d8+2", "type": "bludgeoning"}]))
        self.assertEqual(claw["on_hit"], [{"kind": "grapple", "escape_dc": 12, "max_grapples": 2}])
        self.assertNotIn("restrains", claw["on_hit"][0])
        self.assertNotIn("attack_bonus", claw)
        sting = act("giant-scorpion", "sting")
        self.assertEqual(sting["natural"], {"dice": "1d10", "ability": "str"})
        self.assertEqual((sting["to_hit"], sting["damage"]), (4, [{"dice": "1d10+2", "type": "piercing"}]))
        self.assertEqual(sting["on_hit"], [{
            "kind": "save_condition",
            "save": {"ability": "con", "dc": 12},
            "damage": [{"dice": "4d10", "type": "poison"}],
            "on_success": "half",
        }])
        self.assertNotIn("zero_hp", sting["on_hit"][0])

        mino = load("minotaur")
        self.assertEqual((mino["size"], mino["type"]), ("large", "monstrosity"))
        self.assertEqual(mino["armor_class"]["value"], 14)
        self.assertEqual(mino["natural_armor"], {"base": 14, "dex": True})
        self.assertEqual(mino["hit_points"], {"average": 76, "formula": "9d10+27"})
        self.assertEqual(mino["skills"], {"perception": 7})
        self.assertEqual(mino["senses"], {"darkvision": 60, "passive_perception": 17})
        self.assertNotIn("passive_perception_reason", mino)
        self.assertEqual(mino["hands"], 2)
        self.assertEqual(mino["weapon_dice_multiplier"], 2)
        self.assertEqual([e["item"] for e in mino["equipment"]], ["srd51.item.greataxe"])
        self.assertNotIn("multiattack", {t["id"] for t in mino["traits"]})
        self.assertEqual([a["id"] for a in mino["actions"]], ["greataxe", "gore"])
        self.assertEqual([t["effect"] for t in mino["traits"] if "effect" in t], [{"kind": "reckless_attack"}])
        self.assertNotIn("effect", trait("minotaur", "charge"))
        self.assertNotIn("effect", trait("minotaur", "labyrinthine_recall"))
        self.assertIn("straight toward", trait("minotaur", "charge")["description"])
        self.assertIn("pushed up to 10 ft.", trait("minotaur", "charge")["description"])
        axe = act("minotaur", "greataxe")
        self.assertEqual(axe["weapon"], "srd51.item.greataxe")
        self.assertEqual(axe["use"], "two_hands")
        self.assertNotIn("natural", axe)
        self.assertEqual((axe["to_hit"], axe["damage"]), (6, [{"dice": "2d12+4", "type": "slashing"}]))
        self.assertNotIn("attack_bonus", axe)
        gore = act("minotaur", "gore")
        self.assertEqual(gore["natural"], {"dice": "2d8", "ability": "str"})
        self.assertEqual((gore["to_hit"], gore["damage"]), (6, [{"dice": "2d8+4", "type": "piercing"}]))
        self.assertEqual(gore["on_hit"], act("minotaur-skeleton", "gore")["on_hit"])
        self.assertEqual(gore["on_hit"], [
            {"kind": "extra_damage", "damage": [{"dice": "2d8", "type": "piercing"}], "when": moved},
            {"kind": "save_condition", "save": {"ability": "str", "dc": 14},
             "apply": [{"condition": "prone"}], "when": moved},
        ])
        self.assertNotIn("forced_move", json.dumps(mino))

        hound = load("hell-hound")
        self.assertEqual((hound["size"], hound["type"]), ("medium", "fiend"))
        self.assertEqual(hound["armor_class"]["value"], 15)
        self.assertEqual(hound["natural_armor"], {"base": 14, "dex": True})
        self.assertEqual(hound["hit_points"], {"average": 45, "formula": "7d8+14"})
        self.assertEqual(hound["speed"], {"walk": 50})
        self.assertEqual(hound["skills"], {"perception": 5})
        self.assertEqual(hound["damage_immunities"], ["fire"])
        self.assertEqual(hound["senses"], {"darkvision": 60, "passive_perception": 15})
        self.assertNotIn("passive_perception_reason", hound)
        self.assertNotIn("effect", trait("hell-hound", "keen_hearing_and_smell"))
        self.assertEqual([t["effect"] for t in hound["traits"] if "effect" in t], [{"kind": "pack_tactics"}])
        bite = act("hell-hound", "bite")
        self.assertEqual(bite["natural"], {"dice": "1d8", "ability": "str"})
        self.assertEqual((bite["to_hit"], bite["damage"]), (5, [
            {"dice": "1d8+3", "type": "piercing"},
            {"dice": "2d6", "type": "fire"},
        ]))
        self.assertNotIn("attack_bonus", bite)
        self.assertNotIn("on_hit", bite)
        breath = act("hell-hound", "fire_breath")
        self.assertEqual((breath["kind"], breath["origin"]), ("save_effect", "self"))
        self.assertEqual(breath["area"], {"shape": "cone", "size_ft": 15})
        self.assertEqual(breath["save"], {"ability": "dex", "dc": 12})
        self.assertEqual(breath["damage"], [{"dice": "6d6", "type": "fire"}])
        self.assertEqual((breath["on_success"], breath["usage"]), ("half", {"kind": "recharge", "min": 5}))

        wolf = load("winter-wolf")
        self.assertEqual((wolf["size"], wolf["type"]), ("large", "monstrosity"))
        self.assertEqual(wolf["armor_class"]["value"], 13)
        self.assertEqual(wolf["natural_armor"], {"base": 12, "dex": True})
        self.assertEqual(wolf["hit_points"], {"average": 75, "formula": "10d10+20"})
        self.assertEqual(wolf["speed"], {"walk": 50})
        self.assertEqual(wolf["skills"], {"perception": 5, "stealth": 3})
        self.assertEqual(wolf["damage_immunities"], ["cold"])
        self.assertEqual(wolf["senses"], {"passive_perception": 15})
        self.assertNotIn("darkvision", wolf["senses"])
        self.assertNotIn("passive_perception_reason", wolf)
        self.assertNotIn("effect", trait("winter-wolf", "keen_hearing_and_smell"))
        self.assertNotIn("effect", trait("winter-wolf", "snow_camouflage"))
        self.assertEqual([t["effect"] for t in wolf["traits"] if "effect" in t], [{"kind": "pack_tactics"}])
        wb = act("winter-wolf", "bite")
        self.assertEqual(wb["natural"], {"dice": "2d6", "ability": "str"})
        self.assertEqual((wb["to_hit"], wb["damage"]), (6, [{"dice": "2d6+4", "type": "piercing"}]))
        self.assertEqual(wb["on_hit"], [{
            "kind": "save_condition",
            "save": {"ability": "str", "dc": 14},
            "apply": [{"condition": "prone"}],
        }])
        cold = act("winter-wolf", "cold_breath")
        self.assertEqual((cold["kind"], cold["origin"]), ("save_effect", "self"))
        self.assertEqual(cold["area"], {"shape": "cone", "size_ft": 15})
        self.assertEqual(cold["save"], {"ability": "dex", "dc": 12})
        self.assertEqual(cold["damage"], [{"dice": "4d8", "type": "cold"}])
        self.assertEqual((cold["on_success"], cold["usage"]), ("half", {"kind": "recharge", "min": 5}))

        for idx in ("sea-hag", "mimic", "doppelganger", "green-hag"):
            self.assertFalse((PACK / "monsters" / f"{idx.replace('-', '_')}.json").exists(), idx)

    def test_cr3_batch_n(self):
        # CR 3 batch N. Manticore is green: Tail Spike Regrowth's 24-spike count is text only.
        # Werewolf is the humanoid form (wererat pattern). Phase spider Ethereal Jaunt is text only.
        # Sea hag, mimic, doppelganger and green hag stay deferred.

        def trait(idx, tid):
            return {t["id"]: t for t in load(idx)["traits"]}[tid]

        def act(idx, aid):
            return {a["id"]: a for a in load(idx)["actions"]}[aid]

        def opts(idx):
            return {o["id"]: o["attacks"] for t in load(idx)["traits"] if t.get("effect", {}).get("kind") == "multiattack"
                    for o in t["effect"]["options"]}

        manticore = load("manticore")
        self.assertEqual((manticore["size"], manticore["type"], manticore["hands"]), ("large", "monstrosity", 0))
        self.assertEqual(manticore["alignment"], "lawful evil")
        self.assertEqual(manticore["natural_armor"], {"base": 11, "dex": True})
        self.assertEqual(manticore["armor_class"]["value"], 14)
        self.assertEqual(manticore["speed"], {"walk": 30, "fly": 50})
        self.assertNotIn("hover", manticore["speed"])
        self.assertEqual(manticore["senses"], {"darkvision": 60, "passive_perception": 11})
        self.assertEqual(manticore["languages"], [])
        self.assertNotIn("skills", manticore)
        self.assertNotIn("effect", trait("manticore", "tail_spike_regrowth"))
        self.assertIn("unlimited", trait("manticore", "tail_spike_regrowth")["description"])
        self.assertEqual(opts("manticore"), {
            "bite_claws": ["bite", "claw", "claw"],
            "tail_spikes": ["tail_spike", "tail_spike", "tail_spike"],
        })
        self.assertEqual(act("manticore", "bite")["natural"], {"dice": "1d8", "ability": "str"})
        self.assertEqual(act("manticore", "claw")["natural"], {"dice": "1d6", "ability": "str"})
        spike = act("manticore", "tail_spike")
        self.assertEqual(spike["kind"], "ranged_weapon_attack")
        self.assertEqual(spike["natural"], {"dice": "1d8", "ability": "dex"})
        self.assertEqual((spike["to_hit"], spike["range"], spike["damage"]),
                         (5, [100, 200], [{"dice": "1d8+3", "type": "piercing"}]))
        self.assertNotIn("weapon", spike)
        self.assertNotIn("usage", json.dumps(manticore))

        wolf = load("werewolf")
        self.assertIs(wolf["shapechanger"], True)
        self.assertEqual((wolf["size"], wolf["type"], wolf["tags"]), ("medium", "humanoid", ["human", "shapechanger"]))
        self.assertEqual(wolf["alignment"], "chaotic evil")
        self.assertEqual((wolf["armor_class"], wolf["hands"], wolf["speed"]), ({"value": 11}, 2, {"walk": 30}))
        self.assertNotIn("natural_armor", wolf)
        self.assertEqual(wolf["equipment"], [{"item": "srd51.item.spear", "equipped": True}])
        self.assertEqual(wolf["skills"], {"perception": 4})
        self.assertEqual(wolf["senses"], {"passive_perception": 14})
        self.assertNotIn("darkvision", wolf["senses"])
        self.assertEqual(wolf["languages"], ["Common"])
        self.assertNotIn("damage_immunities", wolf)
        self.assertEqual(trait("werewolf", "damage_immunities_nonmagical")["effect"], {
            "kind": "damage_mitigation",
            "immunities": {"nonmagical_not_silvered": ["bludgeoning", "piercing", "slashing"]},
        })
        self.assertNotIn("effect", trait("werewolf", "shapechanger"))
        self.assertNotIn("forms", trait("werewolf", "shapechanger"))
        self.assertIn("wolf form speed 40", trait("werewolf", "shapechanger")["description"])
        self.assertIn("hybrid/wolf AC 12", trait("werewolf", "shapechanger")["description"])
        self.assertNotIn("effect", trait("werewolf", "keen_hearing_and_smell"))
        self.assertEqual(opts("werewolf"), {
            "spear_x2": ["spear", "spear"],
            "spear_two_handed_x2": ["spear_two_handed", "spear_two_handed"],
            "spear_thrown_x2": ["spear_thrown", "spear_thrown"],
        })
        self.assertEqual(act("werewolf", "bite")["kind"], "special")
        self.assertNotIn("natural", act("werewolf", "bite"))
        self.assertIn("lycanthropy", act("werewolf", "bite")["description"])
        self.assertEqual(act("werewolf", "claws")["kind"], "special")
        self.assertNotIn("natural", act("werewolf", "claws"))
        self.assertIn("2d4 + 2", act("werewolf", "claws")["description"])
        spear = act("werewolf", "spear")
        two = act("werewolf", "spear_two_handed")
        thrown = act("werewolf", "spear_thrown")
        self.assertEqual(spear["weapon"], "srd51.item.spear")
        self.assertEqual(spear["ability"], "str")
        self.assertEqual((spear["to_hit"], spear["damage"]), (4, [{"dice": "1d6+2", "type": "piercing"}]))
        self.assertEqual((two["use"], two["to_hit"], two["damage"]),
                         ("two_hands", 4, [{"dice": "1d8+2", "type": "piercing"}]))
        self.assertEqual((thrown["use"], thrown["range"], thrown["damage"]),
                         ("thrown", [20, 60], [{"dice": "1d6+2", "type": "piercing"}]))

        spider = load("phase-spider")
        self.assertEqual((spider["size"], spider["type"], spider["alignment"], spider["hands"]),
                         ("large", "monstrosity", "unaligned", 0))
        self.assertEqual(spider["natural_armor"], {"base": 11, "dex": True})
        self.assertEqual(spider["armor_class"]["value"], 13)
        self.assertEqual(spider["speed"], {"walk": 30, "climb": 30})
        self.assertEqual(spider["skills"], {"stealth": 6})
        self.assertNotIn("passive_perception_reason", spider)
        self.assertEqual(spider["senses"], {"darkvision": 60, "passive_perception": 10})
        self.assertEqual(spider["languages"], [])
        self.assertNotIn("effect", trait("phase-spider", "ethereal_jaunt"))
        self.assertIn("no Ethereal Plane", trait("phase-spider", "ethereal_jaunt")["description"])
        self.assertNotIn("effect", trait("phase-spider", "spider_climb"))
        self.assertNotIn("effect", trait("phase-spider", "web_walker"))
        self.assertNotIn("web_sense", {t["id"] for t in spider["traits"]})
        self.assertNotIn("bonus_actions", spider)
        self.assertEqual([a["id"] for a in spider["actions"]], ["bite"])
        bite = act("phase-spider", "bite")
        self.assertEqual(bite["natural"], {"dice": "1d10", "ability": "str"})
        self.assertEqual((bite["to_hit"], bite["damage"]), (4, [{"dice": "1d10+2", "type": "piercing"}]))
        self.assertIn("one creature", bite["description"])
        self.assertEqual(bite["on_hit"], [{
            "kind": "save_condition",
            "save": {"ability": "con", "dc": 11},
            "damage": [{"dice": "4d8", "type": "poison"}],
            "on_success": "half",
            "zero_hp": {
                "condition": "poisoned",
                "while": "paralyzed",
                "duration": {"kind": "rounds", "n": 600},
            },
        }])
        self.assertFalse((PACK / "monsters" / "sea_hag.json").exists())
        self.assertFalse((PACK / "monsters" / "mimic.json").exists())
        self.assertFalse((PACK / "monsters" / "doppelganger.json").exists())
        self.assertFalse((PACK / "monsters" / "green_hag.json").exists())

    def test_cr4_batch_o(self):
        # CR 4 batch O. Black Pudding is an honest partial (ochre jelly / gray ooze): Split, Amorphous,
        # Spider Climb, Corrosive Form contact acid, and armor/weapon corrosion stay description only.
        # Deafened follows the SRD line; a6212be omits it. Elephant Stomp is description only.
        # Trampling Charge is the gore's on_hit (lion / elk), not any melee hit. Ettin weapons are naturals.
        # Chuul and ghost are new deferrals. The standing four stay deferred.

        def trait(idx, tid):
            return {t["id"]: t for t in load(idx)["traits"]}[tid]

        def act(idx, aid):
            return {a["id"]: a for a in load(idx)["actions"]}[aid]

        def opts(idx):
            return {o["id"]: o["attacks"] for t in load(idx)["traits"] if t.get("effect", {}).get("kind") == "multiattack"
                    for o in t["effect"]["options"]}

        pudding = load("black-pudding")
        self.assertEqual((pudding["size"], pudding["type"], pudding["alignment"], pudding["hands"]),
                         ("large", "ooze", "unaligned", 0))
        self.assertEqual(pudding["armor_class"], {"value": 7})
        self.assertNotIn("natural_armor", pudding)
        self.assertEqual(pudding["speed"], {"walk": 20, "climb": 20})
        self.assertEqual(pudding["damage_immunities"], ["acid", "cold", "lightning", "slashing"])
        self.assertNotIn("damage_resistances", pudding)
        self.assertEqual(pudding["senses"], {"blindsight": 60, "passive_perception": 8})
        self.assertIsInstance(pudding["senses"]["blindsight"], int)
        self.assertEqual(pudding["languages"], [])
        self.assertNotIn("reactions", pudding)
        for tid in ("amorphous", "corrosive_form", "spider_climb", "split"):
            self.assertNotIn("effect", trait("black-pudding", tid))
        self.assertIn("1d8", trait("black-pudding", "corrosive_form")["description"])
        self.assertIn("Descriptive only", trait("black-pudding", "corrosive_form")["description"])
        self.assertIn("no creature-spawn", trait("black-pudding", "split")["description"])
        self.assertEqual([a["id"] for a in pudding["actions"]], ["pseudopod"])
        pod = act("black-pudding", "pseudopod")
        self.assertEqual(pod["natural"], {"dice": "1d6", "ability": "str"})
        self.assertNotIn("attack_bonus", pod)
        self.assertEqual((pod["to_hit"], pod["damage"]), (5, [
            {"dice": "1d6+3", "type": "bludgeoning"},
            {"dice": "4d8", "type": "acid"},
        ]))
        self.assertIn("armor", pod["description"])
        self.assertIn("descriptive only", pod["description"])

        elephant = load("elephant")
        self.assertEqual((elephant["size"], elephant["type"], elephant["alignment"], elephant["hands"]),
                         ("huge", "beast", "unaligned", 0))
        self.assertEqual(elephant["natural_armor"], {"base": 13, "dex": True})
        self.assertEqual(elephant["armor_class"]["value"], 12)
        self.assertEqual(elephant["speed"], {"walk": 40})
        self.assertEqual(elephant["senses"], {"passive_perception": 10})
        self.assertEqual(elephant["languages"], [])
        self.assertNotIn("skills", elephant)
        self.assertNotIn("effect", trait("elephant", "trampling_charge"))
        charge = trait("elephant", "trampling_charge")["description"]
        self.assertIn("straight toward", charge)
        self.assertIn("gore", charge)
        self.assertIn("3d10 + 6", charge)
        self.assertIn("not an action", charge)
        self.assertNotIn("extra_damage", json.dumps(elephant))
        self.assertEqual([a["id"] for a in elephant["actions"]], ["gore"])
        gore = act("elephant", "gore")
        self.assertEqual(gore["natural"], {"dice": "3d8", "ability": "str"})
        self.assertEqual((gore["to_hit"], gore["damage"]), (8, [{"dice": "3d8+6", "type": "piercing"}]))
        self.assertNotIn("attack_bonus", gore)
        self.assertEqual(gore["on_hit"], [{
            "kind": "save_condition",
            "save": {"ability": "str", "dc": 12},
            "apply": [{"condition": "prone"}],
            "when": [{"kind": "moved_at_least_ft", "ft": 20}],
        }])
        self.assertEqual([a["name"] for a in TABLE["elephant"]["attacks"]], ["Gore", "Stomp"])

        ettin = load("ettin")
        self.assertEqual((ettin["size"], ettin["type"], ettin["alignment"], ettin["hands"]),
                         ("large", "giant", "chaotic evil", 0))
        self.assertEqual(ettin["natural_armor"], {"base": 13, "dex": True})
        self.assertEqual(ettin["armor_class"]["value"], 12)
        self.assertEqual(ettin["speed"], {"walk": 40})
        self.assertEqual(ettin["skills"], {"perception": 4})
        self.assertNotIn("passive_perception_reason", ettin)
        self.assertEqual(ettin["senses"], {"darkvision": 60, "passive_perception": 14})
        self.assertEqual(ettin["languages"], ["Giant", "Orc"])
        self.assertNotIn("equipment", ettin)
        self.assertEqual(trait("ettin", "two_heads")["effect"], {
            "kind": "save_advantage",
            "save_vs_conditions": ["blinded", "charmed", "deafened", "frightened", "stunned", "unconscious"],
        })
        self.assertIn("Perception", trait("ettin", "two_heads")["description"])
        self.assertNotIn("effect", trait("ettin", "wakeful"))
        self.assertEqual(opts("ettin"), {"battleaxe_morningstar": ["battleaxe", "morningstar"]})
        axe = act("ettin", "battleaxe")
        star = act("ettin", "morningstar")
        self.assertEqual(axe["natural"], {"dice": "2d8", "ability": "str"})
        self.assertEqual(star["natural"], {"dice": "2d8", "ability": "str"})
        self.assertNotIn("weapon", axe)
        self.assertNotIn("weapon", star)
        self.assertEqual((axe["to_hit"], axe["damage"]), (7, [{"dice": "2d8+5", "type": "slashing"}]))
        self.assertEqual((star["to_hit"], star["damage"]), (7, [{"dice": "2d8+5", "type": "piercing"}]))

        for idx in ("chuul", "ghost", "sea-hag", "mimic", "doppelganger", "green-hag"):
            self.assertNotIn(idx, TABLE)
            self.assertFalse((PACK / "monsters" / f"{idx.replace('-', '_')}.json").exists(), idx)

    def test_cr4_batch_p(self):
        # CR 4 batch P. Wereboar and weretiger are the humanoid forms (wererat / werewolf pattern).
        # Lamia intoxicating touch and innate spells are text only. Couatl Magic Weapons is live.
        # Succubus stays deferred, with sea hag, mimic, doppelganger and green hag.

        def trait(idx, tid):
            return {t["id"]: t for t in load(idx)["traits"]}[tid]

        def act(idx, aid):
            return {a["id"]: a for a in load(idx)["actions"]}[aid]

        def opts(idx):
            return {o["id"]: o["attacks"] for t in load(idx)["traits"] if t.get("effect", {}).get("kind") == "multiattack"
                    for o in t["effect"]["options"]}

        boar = load("wereboar")
        self.assertIs(boar["shapechanger"], True)
        self.assertEqual((boar["size"], boar["type"], boar["tags"], boar["alignment"]),
                         ("medium", "humanoid", ["human", "shapechanger"], "neutral evil"))
        self.assertEqual((boar["armor_class"], boar["hands"], boar["speed"]), ({"value": 10}, 2, {"walk": 30}))
        self.assertNotIn("natural_armor", boar)
        self.assertEqual(boar["equipment"], [{"item": "srd51.item.maul", "equipped": True}])
        self.assertEqual(boar["hit_points"], {"average": 78, "formula": "12d8+24"})
        self.assertEqual(boar["skills"], {"perception": 2})
        self.assertEqual(boar["senses"], {"passive_perception": 12})
        self.assertNotIn("darkvision", boar["senses"])
        self.assertNotIn("damage_immunities", boar)
        self.assertEqual(trait("wereboar", "damage_immunities_nonmagical")["effect"], {
            "kind": "damage_mitigation",
            "immunities": {"nonmagical_not_silvered": ["bludgeoning", "piercing", "slashing"]},
        })
        self.assertNotIn("effect", trait("wereboar", "shapechanger"))
        self.assertIn("boar form speed 40", trait("wereboar", "shapechanger")["description"])
        self.assertIn("hybrid/boar AC 11", trait("wereboar", "shapechanger")["description"])
        self.assertNotIn("effect", trait("wereboar", "charge"))
        self.assertIn("15 feet", trait("wereboar", "charge")["description"])
        rel = trait("wereboar", "relentless")
        self.assertEqual(rel["name"], "Relentless (Recharges after a Short or Long Rest)")
        self.assertEqual(rel["effect"], {
            "kind": "on_event",
            "on": "drop_to_zero",
            "when": [{"kind": "damage_at_most", "amount": 14}],
            "usage": {"kind": "per_rest", "uses": 1, "rest": "short"},
            "effects": [{"kind": "set_hp", "value": 1}],
        })
        self.assertEqual(opts("wereboar"), {"maul_x2": ["maul", "maul"]})
        maul = act("wereboar", "maul")
        self.assertEqual((maul["weapon"], maul["use"], maul["to_hit"], maul["damage"]),
                         ("srd51.item.maul", "two_hands", 5, [{"dice": "2d6+3", "type": "bludgeoning"}]))
        self.assertEqual(act("wereboar", "tusks")["kind"], "special")
        self.assertNotIn("natural", act("wereboar", "tusks"))
        self.assertIn("lycanthropy", act("wereboar", "tusks")["description"])

        tiger = load("weretiger")
        self.assertIs(tiger["shapechanger"], True)
        self.assertEqual((tiger["size"], tiger["type"], tiger["tags"], tiger["alignment"]),
                         ("medium", "humanoid", ["human", "shapechanger"], "neutral"))
        self.assertEqual((tiger["armor_class"], tiger["hands"], tiger["speed"]), ({"value": 12}, 2, {"walk": 30}))
        self.assertNotIn("natural_armor", tiger)
        self.assertEqual(tiger["equipment"], [
            {"item": "srd51.item.scimitar", "equipped": True},
            {"item": "srd51.item.longbow"},
        ])
        self.assertEqual(tiger["hit_points"], {"average": 120, "formula": "16d8+48"})
        self.assertEqual(tiger["skills"], {"perception": 5, "stealth": 4})
        self.assertEqual(tiger["senses"], {"darkvision": 60, "passive_perception": 15})
        self.assertNotIn("damage_immunities", tiger)
        self.assertEqual(trait("weretiger", "damage_immunities_nonmagical")["effect"]["immunities"],
                         {"nonmagical_not_silvered": ["bludgeoning", "piercing", "slashing"]})
        self.assertNotIn("effect", trait("weretiger", "shapechanger"))
        self.assertIn("tiger form speed 40", trait("weretiger", "shapechanger")["description"])
        self.assertNotIn("effect", trait("weretiger", "keen_hearing_and_smell"))
        self.assertNotIn("effect", trait("weretiger", "pounce"))
        self.assertIn("DC 14", trait("weretiger", "pounce")["description"])
        self.assertEqual(opts("weretiger"), {
            "scimitar_x2": ["scimitar", "scimitar"],
            "longbow_x2": ["longbow", "longbow"],
        })
        scimitar = act("weretiger", "scimitar")
        self.assertEqual((scimitar["weapon"], scimitar["ability"], scimitar["to_hit"], scimitar["damage"]),
                         ("srd51.item.scimitar", "str", 5, [{"dice": "1d6+3", "type": "slashing"}]))
        bow = act("weretiger", "longbow")
        self.assertEqual((bow["weapon"], bow["ability"], bow["to_hit"], bow["range"], bow["damage"]),
                         ("srd51.item.longbow", "dex", 4, [150, 600], [{"dice": "1d8+2", "type": "piercing"}]))
        self.assertEqual(act("weretiger", "bite")["kind"], "special")
        self.assertIn("lycanthropy", act("weretiger", "bite")["description"])
        self.assertEqual(act("weretiger", "claw")["kind"], "special")
        self.assertNotIn("natural", act("weretiger", "claw"))

        lamia = load("lamia")
        self.assertNotIn("shapechanger", lamia)
        self.assertEqual((lamia["size"], lamia["type"], lamia["alignment"], lamia["hands"]),
                         ("large", "monstrosity", "chaotic evil", 2))
        self.assertEqual(lamia["natural_armor"], {"base": 12, "dex": True})
        self.assertEqual(lamia["armor_class"]["value"], 13)
        self.assertEqual(lamia["hit_points"], {"average": 97, "formula": "13d10+26"})
        self.assertEqual(lamia["speed"], {"walk": 30})
        self.assertEqual(lamia["skills"], {"deception": 7, "insight": 4, "stealth": 3})
        self.assertEqual(lamia["senses"], {"darkvision": 60, "passive_perception": 12})
        self.assertEqual(lamia["languages"], ["Abyssal", "Common"])
        self.assertEqual(lamia["equipment"], [{"item": "srd51.item.dagger", "equipped": True}])
        self.assertNotIn("effect", trait("lamia", "innate_spellcasting"))
        self.assertNotIn("spellcasting", lamia)
        self.assertIn("geas", trait("lamia", "innate_spellcasting")["description"])
        self.assertEqual(opts("lamia"), {"claws_dagger": ["claws", "dagger"]})
        self.assertNotIn("intoxicating_touch", opts("lamia")["claws_dagger"])
        claws = act("lamia", "claws")
        self.assertEqual(claws["natural"], {"dice": "2d10", "ability": "str"})
        self.assertEqual((claws["to_hit"], claws["damage"]), (5, [{"dice": "2d10+3", "type": "slashing"}]))
        dagger = act("lamia", "dagger")
        self.assertEqual((dagger["weapon"], dagger["ability"], dagger["to_hit"], dagger["damage"]),
                         ("srd51.item.dagger", "str", 5, [{"dice": "1d4+3", "type": "piercing"}]))
        self.assertNotIn("use", dagger)
        touch = act("lamia", "intoxicating_touch")
        self.assertEqual(touch["kind"], "special")
        self.assertNotIn("effect", touch)
        self.assertIn("disadvantage on Wisdom", touch["description"])

        couatl = load("couatl")
        self.assertNotIn("shapechanger", couatl)
        self.assertEqual((couatl["size"], couatl["type"], couatl["alignment"], couatl["hands"]),
                         ("medium", "celestial", "lawful good", 0))
        self.assertEqual(couatl["natural_armor"], {"base": 14, "dex": True})
        self.assertEqual(couatl["armor_class"]["value"], 19)
        self.assertEqual(couatl["hit_points"], {"average": 97, "formula": "13d8+39"})
        self.assertEqual(couatl["speed"], {"walk": 30, "fly": 90})
        self.assertNotIn("hover", couatl["speed"])
        self.assertEqual(couatl["saving_throws"], {"con": 5, "wis": 7, "cha": 6})
        self.assertEqual(couatl["damage_resistances"], ["radiant"])
        self.assertEqual(couatl["damage_immunities"], ["psychic"])
        self.assertEqual(couatl["senses"], {"truesight": 120, "passive_perception": 15})
        self.assertEqual(couatl["languages"], ["all", "telepathy 120 ft."])
        self.assertNotIn("spellcasting", couatl)
        self.assertNotIn("effect", trait("couatl", "innate_spellcasting"))
        self.assertNotIn("effect", trait("couatl", "shielded_mind"))
        self.assertEqual(trait("couatl", "magic_weapons")["effect"], {"kind": "magic_weapons"})
        self.assertEqual(trait("couatl", "damage_immunities_nonmagical")["effect"], {
            "kind": "damage_mitigation",
            "immunities": {"nonmagical": ["bludgeoning", "piercing", "slashing"]},
        })
        self.assertFalse(any(t.get("effect", {}).get("kind") == "multiattack" for t in couatl["traits"]))
        bite = act("couatl", "bite")
        self.assertEqual(bite["natural"], {"dice": "1d6", "ability": "dex"})
        self.assertEqual(bite["attack_bonus"]["value"], 8)
        self.assertEqual((bite["to_hit"], bite["damage"]), (8, [{"dice": "1d6+5", "type": "piercing"}]))
        self.assertEqual(bite["on_hit"], [{
            "kind": "save_condition",
            "save": {"ability": "con", "dc": 13},
            "apply": [{"condition": "poisoned", "duration": {"kind": "rounds", "n": 14400}}],
        }])
        self.assertIn("unconscious", bite["description"])
        constrict = act("couatl", "constrict")
        self.assertEqual(constrict["natural"], {"dice": "2d6", "ability": "str"})
        self.assertEqual(constrict["attack_bonus"]["value"], 6)
        self.assertEqual((constrict["to_hit"], constrict["reach"], constrict["damage"]),
                         (6, 10, [{"dice": "2d6+3", "type": "bludgeoning"}]))
        self.assertEqual(constrict["on_hit"], [{
            "kind": "grapple", "escape_dc": 15, "restrains": True, "exclusive": True,
        }])
        self.assertNotIn("max_size", constrict["on_hit"][0])
        self.assertIn("Medium or smaller", constrict["description"])
        shape = act("couatl", "change_shape")
        self.assertEqual(shape["kind"], "special")
        self.assertNotIn("effect", shape)
        self.assertIn("shapechanger is not set", shape["description"])

        for idx in ("sea-hag", "mimic", "doppelganger", "green-hag", "succubus", "succubus-incubus"):
            self.assertFalse((PACK / "monsters" / f"{idx.replace('-', '_')}.json").exists(), idx)

    def test_authored_and_deferred_split(self):
        have = sorted(p.stem for p in (PACK / "monsters").glob("*.json"))
        self.assertEqual((len(AUTHORED), len(DEFERRED)), (173, 7))
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
