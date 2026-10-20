#!/usr/bin/env python3
"""SRD 5.1 Fighter (Champion), Cleric (Life), Wizard (Evocation), Sorcerer (Draconic Bloodline), Rogue (Thief), Paladin (Oath of Devotion), Ranger (Hunter), Barbarian (Path of the Berserker) and Bard (College of Lore) levels 1-5 in srd51.core vs the 5e-database
tables (ctest: content_validate_srd51_classes).

`data/srd51_classes_5e_database_a6212be.json` holds fields copied verbatim from 5e-bits/5e-database commit a6212be
(src/2014/en/5e-SRD-Classes.json, -Levels.json, -Features.json, -Subclasses.json, and -Spells.json for the spells the
pack ships). One character@4 document per class per level (`srd51.character.<class>_level_<n>`), a human pre-generated
PC built from the standard array (+1 to every score, SRD human) with the ASI at level 4 on the class's main ability.

Asserted per document: class / subclass / hit die / level / proficiency bonus / XP threshold, saving throws and skills
(ability modifier + proficiency), hit points (max die at level 1, then the fixed average + Con), AC and every weapon
attack derived from the pack's own item@1 documents, spell slots and caster level, and that every spell is a shipped
spell@1 on the class (or domain) list. Also pins the authored/deferred split of every class and subclass feature of
levels 1-5 (docs/SRD_CLASSES_DEFERRED.md) and the mechanics that are data, not text.
"""
from __future__ import annotations

import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACK = ROOT / "content/packs/srd51.core"
DATA = json.loads((Path(__file__).parent / "data/srd51_classes_5e_database_a6212be.json").read_text(encoding="utf-8"))
TABLE, SPELLS = DATA["classes"], DATA["spells"]
LEVELS = range(1, 6)
XP = {1: 0, 2: 300, 3: 900, 4: 2700, 5: 6500}   # SRD 5.1 Character Advancement table
SUBCLASS_AT = {"fighter": 3, "cleric": 1, "wizard": 2, "sorcerer": 1, "rogue": 3, "paladin": 3, "ranger": 3, "barbarian": 3, "bard": 3}
SPELLCASTING_FROM = {"paladin": 2, "ranger": 2}   # half caster: no spellcasting block at level 1
MAIN = {"fighter": "str", "cleric": "wis", "wizard": "int", "sorcerer": "cha", "rogue": "dex", "paladin": "str", "ranger": "dex", "barbarian": "str", "bard": "cha"}   # the ASI at level 4 goes here (+2)
SKILLS = {"athletics": "str", "perception": "wis", "insight": "wis", "religion": "int", "arcana": "int", "history": "int",
          "persuasion": "cha", "acrobatics": "dex", "sleight_of_hand": "dex", "stealth": "dex", "survival": "wis", "deception": "cha", "performance": "cha"}
# Rogue: four class skills (two for the others) and Expertise (doubled proficiency) on Stealth and Perception.
SKILL_COUNT = {"rogue": 4, "ranger": 3}
# Bard: three class skills, plus three from College of Lore Bonus Proficiencies at 3rd; Expertise (Performance, Persuasion) at 3rd.
def skill_count(cls: str, level: int) -> int:
    return (6 if level >= 3 else 3) if cls == "bard" else SKILL_COUNT.get(cls, 2)
EXPERTISE_FROM = {"bard": 3}
EXPERTISE = {"rogue": {"stealth", "perception"}, "bard": {"performance", "persuasion"}}
# Rogue (Thief) Second-Story Work from 3rd: climbing costs no extra movement = a climb speed equal to the walking speed.
# Barbarian Fast Movement from 5th: +10 ft walking speed (no heavy armor), baked.
def speed(cls: str, level: int) -> dict:
    if cls == "barbarian":
        return {"walk": 40 if level >= 5 else 30}
    return {"walk": 30, "climb": 30} if cls == "rogue" and level >= 3 else {"walk": 30}
# Draconic Resilience (sorcerer, Draconic Bloodline): +1 hit point per sorcerer level, and AC 13 + Dex without armor.
HP_PER_LEVEL = {"sorcerer": 1}
UNARMORED_BASE = {"sorcerer": 13}
# Barbarian Unarmored Defense: 10 + Dex + Con with no armor (a shield still allowed), baked into armor_class.
UNARMORED_CON = {"barbarian"}

# Every class / subclass feature of levels 1-5 (mirror index) -> where it lives. "feature:<id>" = a features[] entry;
# "subclass" = classes[0].subclass; "spellcasting" = the spellcasting block; "abilities" = baked into the scores.
AUTHORED = {
    "fighter-fighting-style": "feature:fighting_style_dueling",
    "second-wind": "feature:second_wind",
    "action-surge-1-use": "feature:action_surge",
    "martial-archetype": "subclass",
    "improved-critical": "feature:improved_critical",
    "fighter-ability-score-improvement-1": "abilities",
    "extra-attack-1": "feature:extra_attack",
    "spellcasting-cleric": "spellcasting",
    "divine-domain": "subclass",
    "divine-domain-improvement-1": "subclass",
    "domain-spells-1": "spellcasting",
    "domain-spells-2": "spellcasting",
    "domain-spells-3": "spellcasting",
    "bonus-proficiency": "feature:bonus_proficiency",
    "cleric-ability-score-improvement-1": "abilities",
    "spellcasting-wizard": "spellcasting",
    "arcane-tradition": "subclass",
    "arcane-recovery": "feature:arcane_recovery",   # slot_recovery kind (7.2n, sim #148 loader)
    "evocation-savant": "feature:evocation_savant",
    "sculpt-spells": "feature:sculpt_spells",   # R-G14 (sim #143): open kind sculpt_spells {}
    "wizard-ability-score-improvement-1": "abilities",
    "spellcasting-sorcerer": "spellcasting",
    "sorcerous-origin": "subclass",
    "dragon-ancestor": "feature:dragon_ancestor",            # descriptive (Draconic language; the damage type matters at 6+)
    "draconic-resilience": "feature:draconic_resilience",    # baked into hit_points / armor_class (HP_PER_LEVEL, UNARMORED_BASE)
    "font-of-magic": "feature:font_of_magic",                # font_of_magic kind (7.2o, sim #152) + resources[] sorcery_points
    "flexible-casting-creating-spell-slots": "feature:font_of_magic",   # rules5e FlexibleCasting, enabled by font_of_magic
    "flexible-casting-converting-spell-slot": "feature:font_of_magic",
    "metamagic-1": "feature:metamagic",                      # metamagic kind (R-G14, sim #143), the same pool
    "sorcerer-ability-score-improvement-1": "abilities",
    "rogue-expertise-1": "feature:expertise",                # descriptive; baked into skills (EXPERTISE) and passive Perception
    "sneak-attack": "feature:sneak_attack",                  # on_event hit, once_per_turn, finesse_or_ranged + advantage_or_ally_adjacent
    "thieves-cant": "feature:thieves_cant",                  # descriptive (a language)
    "cunning-action": "feature:cunning_action",              # bonus_action_grant [dash, disengage, hide]
    "roguish-archetype": "subclass",
    "rogue-ability-score-improvement-1": "abilities",
    "fast-hands": "feature:fast_hands",                      # descriptive: no Use an Object / Sleight of Hand / thieves' tools action
    "second-story-work": "feature:second_story_work",        # partial: climb speed in speed; the running-jump bonus is descriptive
    "uncanny-dodge": "feature:uncanny_dodge",
    "divine-sense": "feature:divine_sense",                  # descriptive: no sensing / reveal mechanic
    "lay-on-hands": "feature:lay_on_hands",                  # heal_pool + resources[] lay_on_hands (5 x paladin level, long rest); partial
    "paladin-fighting-style": "feature:fighting_style_dueling",
    "spellcasting-paladin": "spellcasting",
    "divine-smite": "feature:divine_smite",                  # on_event hit (melee weapon), ask, spend slot any, 2d8 + 1d8 / slot level; partial
    "divine-health": "feature:divine_health",                # descriptive: diseases are not modelled
    "sacred-oath": "subclass",
    "oath-spells": "spellcasting",
    "channel-divinity": "feature:channel_divinity",          # descriptive: no loaded effect spends the pool (SRD_CLASSES_DEFERRED)
    "channel-divinity-sacred-weapon": "feature:sacred_weapon",      # descriptive: apply_modifier + usage pool is not loaded
    "channel-divinity-turn-the-unholy": "feature:turn_the_unholy",  # descriptive: no character key reaches the 7.2v turned control
    "paladin-ability-score-improvement-1": "abilities",
    "paladin-extra-attack": "feature:extra_attack",
    "favored-enemy-1-type": "feature:favored_enemy",         # descriptive: tracking / recall checks and a language, no combat rule
    "natural-explorer-1-terrain-type": "feature:natural_explorer",  # descriptive: travel and exploration in the favored terrain
    "ranger-fighting-style": "feature:fighting_style_archery",      # attack_bonus {ranged, 2} (R-G5, sim #107)
    "spellcasting-ranger": "spellcasting",
    "ranger-archetype": "subclass",
    "primeval-awareness": "feature:primeval_awareness",      # descriptive: 1-6 mile creature-type sensing, no slot-spending feature key
    "ranger-ability-score-improvement-1": "abilities",
    "ranger-extra-attack": "feature:extra_attack",
    "hunters-prey": "feature:colossus_slayer",
    "rage": "feature:rage",                                  # descriptive: apply_modifier + usage pool is not loaded (SRD_CLASSES_DEFERRED)
    "barbarian-unarmored-defense": "feature:unarmored_defense",   # baked into armor_class (UNARMORED_CON); no effect
    "reckless-attack": "feature:reckless_attack",            # reckless_attack kind (sim #129)
    "danger-sense": "feature:danger_sense",                  # descriptive: no static save-advantage key with a "can see" gate
    "primal-path": "subclass",
    "barbarian-ability-score-improvement-1": "abilities",
    "barbarian-extra-attack": "feature:extra_attack",
    "fast-movement": "feature:fast_movement",                # baked into speed.walk 40 (speed()); no effect
    "frenzy": "feature:frenzy",
    "spellcasting-bard": "spellcasting",
    "bardic-inspiration-d6": "feature:bardic_inspiration",   # descriptive (no key grants a die to an ally); pool from 3rd for Cutting Words
    "bardic-inspiration-d8": "feature:bardic_inspiration",
    "jack-of-all-trades": "feature:jack_of_all_trades",      # descriptive: no half-proficiency key (initiative, unproficient checks)
    "song-of-rest-d6": "feature:song_of_rest",               # descriptive: short-rest hit dice healing, out of combat scope
    "bard-expertise-1": "feature:expertise",                 # baked into skills (EXPERTISE / EXPERTISE_FROM)
    "bard-college": "subclass",
    "bonus-proficiencies": "feature:bonus_proficiencies",    # baked into skills (skill_count)
    "cutting-words": "reactions",                            # reactions[] roll_attack / subtract_roll 1d6 (1d8 at 5), pool bardic_inspiration
    "bard-ability-score-improvement-1": "abilities",
    "font-of-inspiration": "feature:font_of_inspiration",    # the pool's recharge short_rest at 5                              # descriptive: needs Rage, a bonus-action attack while raging, exhaustion on end               # Colossus Slayer chosen; descriptive: no "target below its hit point maximum" predicate                # descriptive: the damaged window is not attack-only (SRD_CLASSES_DEFERRED)
}
# Kept as a features[] entry with no effect: no combat rule, or a mechanic the engine lacks (docs/SRD_CLASSES_DEFERRED.md).
DESCRIPTIVE = {"rogue": ["expertise", "thieves_cant", "fast_hands", "second_story_work", "uncanny_dodge"],
               "paladin": ["divine_sense", "divine_health", "channel_divinity", "sacred_weapon", "turn_the_unholy"],
               "ranger": ["favored_enemy", "natural_explorer", "primeval_awareness", "colossus_slayer"],
               "barbarian": ["rage", "unarmored_defense", "danger_sense", "fast_movement", "frenzy"],
               "bard": ["bardic_inspiration", "jack_of_all_trades", "song_of_rest", "expertise", "bonus_proficiencies", "font_of_inspiration"]}
# Not expressible today (docs/SRD_CLASSES_DEFERRED.md): mirror index -> the feature id it would get (must be absent).
DEFERRED = {
    "disciple-of-life": "disciple_of_life",
    "channel-divinity-1-rest": "channel_divinity",
    "channel-divinity-turn-undead": "turn_undead",
    "channel-divinity-preserve-life": "preserve_life",
    "destroy-undead-cr-1-2-or-below": "destroy_undead",
}
# Life domain spells and class cantrips the pack does not ship yet (listed in the deferred doc).
DEFERRED_SPELLS = ["bless", "spiritual-weapon", "beacon-of-hope", "protection-from-evil-and-good", "sanctuary", "zone-of-truth"]   # Lesser Restoration, Revivify: spells batch 2


def mod(score: int) -> int:
    return (score - 10) // 2


def load(cls: str, level: int) -> dict:
    return json.loads((PACK / "characters" / f"{cls}_level_{level}.json").read_text(encoding="utf-8"))


def item(ref: str) -> dict:
    assert ref.startswith("srd51.item."), ref
    return json.loads((PACK / "items" / f"{ref.removeprefix('srd51.item.')}.json").read_text(encoding="utf-8"))


def docs():
    for cls in TABLE:
        for level in LEVELS:
            yield cls, level, load(cls, level)


def features(d: dict) -> dict:
    return {f["id"]: f for f in d.get("features", [])}


def dice_plus(die: str, bonus: int) -> str:
    return die if bonus == 0 else f"{die}{bonus:+d}"


class ClassTable(unittest.TestCase):
    def test_identity_and_progression(self):
        for cls, level, d in docs():
            with self.subTest(cls=cls, level=level):
                t, row = TABLE[cls], TABLE[cls]["levels"][str(level)]
                self.assertEqual(d["$schema"], "character@4")
                self.assertEqual(d["id"], f"srd51.character.{cls}_level_{level}")
                self.assertEqual(d["level"], level)
                self.assertEqual(len(d["classes"]), 1)
                c = d["classes"][0]
                self.assertEqual((c["class"], c["level"], c["hit_die"]), (cls, level, f"d{t['hit_die']}"))
                self.assertEqual(c.get("subclass"), t["subclass"] if level >= SUBCLASS_AT[cls] else None)
                self.assertEqual(d["proficiency_bonus"], row["prof_bonus"])
                self.assertEqual(d["xp"], XP[level])
                self.assertEqual((d["race"], d["size"], d["type"], d["speed"]), ("human", "medium", "humanoid", speed(cls, level)))
                self.assertEqual(d["source"], {"ref": "SRD 5.1", "license": "CC-BY-4.0"})
                self.assertTrue(d["name"].startswith(f"{t['name']}, level {level}"))

    def test_hit_points(self):
        for cls, level, d in docs():
            with self.subTest(cls=cls, level=level):
                hd, con = TABLE[cls]["hit_die"], mod(d["abilities"]["con"])
                extra = HP_PER_LEVEL.get(cls, 0)
                self.assertEqual(d["hit_points"]["average"], hd + con + extra + (level - 1) * (hd // 2 + 1 + con + extra))
                self.assertEqual(d["hit_points"]["formula"], f"{level}d{hd}+{level * (con + extra)}")

    def test_abilities_and_asi(self):
        for cls in TABLE:
            base = load(cls, 1)["abilities"]
            self.assertEqual(sorted(base.values()), [9, 11, 13, 14, 15, 16])   # standard array, +1 each (human)
            for level in LEVELS:
                with self.subTest(cls=cls, level=level):
                    asis = TABLE[cls]["levels"][str(level)]["ability_score_bonuses"]   # the mirror's running count
                    want = dict(base)
                    want[MAIN[cls]] += 2 * asis
                    self.assertEqual(load(cls, level)["abilities"], want)

    def test_saves_skills_senses(self):
        for cls, level, d in docs():
            with self.subTest(cls=cls, level=level):
                ab, pb = d["abilities"], d["proficiency_bonus"]
                self.assertEqual(d["saving_throws"], {s: mod(ab[s]) + pb for s in TABLE[cls]["saving_throws"]})
                self.assertEqual(len(d["skills"]), skill_count(cls, level))
                expert = EXPERTISE.get(cls, set()) if level >= EXPERTISE_FROM.get(cls, 1) else set()
                for sk, v in d["skills"].items():
                    self.assertEqual(v, mod(ab[SKILLS[sk]]) + pb * (2 if sk in expert else 1), sk)
                self.assertEqual(d["senses"]["passive_perception"], 10 + d["skills"].get("perception", mod(ab["wis"])))

    def test_armor_class_from_items(self):
        for cls, level, d in docs():
            with self.subTest(cls=cls, level=level):
                worn = [item(e["item"]) for e in d["equipment"] if isinstance(e, dict) and e.get("equipped")]
                armor = [i for i in worn if i["category"] == "armor"]
                dex = mod(d["abilities"]["dex"])
                if armor:
                    a = armor[0]
                    ac = a["base_ac"] + (min(dex, a.get("dex_cap", dex)) if a["dex_bonus"] else 0)
                    self.assertLessEqual(a.get("str_min", 0), d["abilities"]["str"])
                else:
                    ac = UNARMORED_BASE.get(cls, 10) + dex + (mod(d["abilities"]["con"]) if cls in UNARMORED_CON else 0)
                ac += sum(i["ac_bonus"] for i in worn if i["category"] == "shield")
                self.assertEqual(d["armor_class"]["value"], ac)

    def test_attacks_from_items(self):
        """Every weapon action is an equipment item: to-hit = ability modifier + proficiency (all three classes are
        proficient with their weapons), damage = the item's die + the same modifier (base damage, no feature folded in)."""
        for cls, level, d in docs():
            items = {i["id"].removeprefix("srd51.item."): i for i in (item(e["item"]) for e in d["equipment"] if isinstance(e, dict))}
            for a in d["actions"]:
                with self.subTest(cls=cls, level=level, action=a["id"]):
                    key = {"light_crossbow": "crossbow_light", "dagger_thrown": "dagger"}.get(a["id"], a["id"])
                    it = items[key]
                    props = it.get("properties", [])
                    ab = d["abilities"]
                    if "finesse" in props:
                        m = max(mod(ab["str"]), mod(ab["dex"]))
                    else:
                        m = mod(ab["dex"]) if a["kind"] == "ranged_weapon_attack" and "thrown" not in props else mod(ab["str"])
                    self.assertEqual(a["to_hit"], m + d["proficiency_bonus"])
                    self.assertEqual(a["damage"], [{"dice": dice_plus(it["damage"][0]["dice"], m), "type": it["damage"][0]["type"]}])
                    self.assertEqual(a.get("properties", []), props)
                    if a["kind"] == "melee_weapon_attack":
                        self.assertEqual(a["reach"], it["reach"])
                    else:
                        self.assertEqual(a["range"], it["range"])

    def test_spellcasting(self):
        for cls, level, d in docs():
            with self.subTest(cls=cls, level=level):
                row, t = TABLE[cls]["levels"][str(level)], TABLE[cls]
                sc = d.get("spellcasting")
                if t["spellcasting_ability"] is None or level < SPELLCASTING_FROM.get(cls, 1):
                    self.assertIsNone(sc)
                    continue
                self.assertEqual((sc["ability"], sc["caster_level"]), (t["spellcasting_ability"], level))
                self.assertEqual(sc["slots"], row["spell_slots"])
                self.assertNotIn("dc", sc)              # derived: 8 + proficiency + modifier
                self.assertNotIn("attack_bonus", sc)
                top = max(int(k) for k in row["spell_slots"])
                domain = {s["spell"] for s in t.get("subclass_spells", []) if s["level"] <= level}
                cantrips = 0
                for ref in sc["spells"]:
                    idx = ref.removeprefix("srd51.spell.").replace("_", "-")
                    self.assertTrue((PACK / "spells" / f"{ref.removeprefix('srd51.spell.')}.json").exists(), ref)
                    sp = SPELLS[idx]
                    self.assertTrue(cls in sp["classes"] or idx in domain, ref)
                    self.assertLessEqual(sp["level"], top, ref)
                    cantrips += sp["level"] == 0
                self.assertLessEqual(cantrips, row.get("cantrips_known", 0))
                for s in domain:   # domain spells are always prepared: present unless deferred (not shipped)
                    self.assertTrue(f"srd51.spell.{s.replace('-', '_')}" in sc["spells"] or s in DEFERRED_SPELLS, s)

    def test_mechanics_are_data(self):
        for level in LEVELS:
            f = features(load("fighter", level))
            self.assertEqual(f["fighting_style_dueling"]["effect"],
                             {"kind": "damage_bonus", "amount": 2, "condition": "one_handed_melee_no_other_weapon"})
            self.assertEqual(f["second_wind"]["effect"], {"kind": "self_heal", "dice": "1d10", "plus": "fighter_level", "uses": 1,
                                                          "recharge": "short_rest", "cost": "bonus_action"})
            self.assertEqual("action_surge" in f, level >= 2)
            if level >= 2:
                self.assertEqual(f["action_surge"]["effect"], {"kind": "grant_action", "cost": "free",
                                                               "usage": {"kind": "per_rest", "uses": 1, "rest": "short"}})
            self.assertEqual("improved_critical" in f, level >= 3)
            if level >= 3:
                self.assertEqual(f["improved_critical"]["effect"], {"kind": "crit_range", "min": 19})
            self.assertEqual("extra_attack" in f, level >= 5)
            if level >= 5:
                # EC #179: the real Extra Attack kind (Combatant::attacks_per_action), not a declared multiattack sequence
                self.assertEqual(f["extra_attack"]["effect"], {"kind": "extra_attack", "attacks_per_action": 2})
        for level in LEVELS:   # Evocation (Arcane Tradition at 2): Sculpt Spells is the fieldless sim #143 kind
            f = features(load("wizard", level))
            self.assertEqual("sculpt_spells" in f, level >= 2)
            if level >= 2:
                self.assertEqual(f["sculpt_spells"]["effect"], {"kind": "sculpt_spells"})
        # Sorcerer (Draconic): font_of_magic (7.2o) from 2nd, metamagic (R-G14) from 3rd, both on the one limited resources[] pool
        # sorcery_points whose max is the sorcerer level (the mirror's sorcery_points column); level 1 has neither.
        for level in LEVELS:
            with self.subTest(sorcerer=level):
                d = load("sorcerer", level)
                f = features(d)
                pts = TABLE["sorcerer"]["levels"][str(level)]["class_specific"]["sorcery_points"]
                if level >= 2:
                    self.assertEqual(d["resources"], [{"id": "sorcery_points", "name": "Sorcery Points", "max": pts,
                                                       "recharge": "long_rest"}])
                    self.assertEqual(pts, level)
                    self.assertEqual(f["font_of_magic"]["effect"], {"kind": "font_of_magic", "pool": "sorcery_points"})
                else:
                    self.assertNotIn("resources", d)
                    self.assertNotIn("font_of_magic", f)
                self.assertEqual("metamagic" in f, level >= 3)
                if level >= 3:
                    eff = f["metamagic"]["effect"]
                    self.assertEqual(eff, {"kind": "metamagic", "options": ["quickened", "twinned"], "pool": "sorcery_points"})
                    self.assertEqual(len(eff["options"]), TABLE["sorcerer"]["levels"][str(level)]["class_specific"]["metamagic_known"])
                ids = [x["id"] for x in d["features"]]
                if level >= 3:
                    self.assertLess(ids.index("font_of_magic"), ids.index("metamagic"))
                leveled = [r for r in d["spellcasting"]["spells"]
                           if SPELLS[r.removeprefix("srd51.spell.").replace("_", "-")]["level"] > 0]
                self.assertLessEqual(len(leveled), TABLE["sorcerer"]["levels"][str(level)]["spells_known"])
        # descriptive entries carry no effect (no combat rule; the heavy-armor proficiency is the worn chain mail)
        for cls, fid in (("cleric", "bonus_proficiency"), ("wizard", "evocation_savant"), ("sorcerer", "dragon_ancestor"),
                         ("sorcerer", "draconic_resilience")):
            for level in LEVELS:
                f = features(load(cls, level))
                if fid in f:
                    self.assertNotIn("effect", f[fid])

    def test_rogue(self):
        sneak = {"kind": "on_event", "on": "hit", "limit": "once_per_turn",
                 "when": [{"kind": "weapon", "is": "finesse_or_ranged"}, {"kind": "advantage_or_ally_adjacent"}],
                 "effects": [{"kind": "extra_damage", "damage": [{"dice": "1d6", "type": "same_as_weapon"}],
                              "scale": {"per_levels": {"class": "rogue", "every": 2, "add_dice": "1d6"}}}]}
        for level in LEVELS:
            with self.subTest(level=level):
                d = load("rogue", level)
                f = features(d)
                self.assertEqual(f["sneak_attack"]["effect"], sneak)
                # the scale gives 1 + (level - 1) // 2 dice: the mirror's Sneak Attack column
                sa = TABLE["rogue"]["levels"][str(level)]["class_specific"]["sneak_attack"]
                self.assertEqual((1 + (level - 1) // 2, 6), (sa["dice_count"], sa["dice_value"]))
                self.assertEqual("cunning_action" in f, level >= 2)
                if level >= 2:
                    self.assertEqual(f["cunning_action"]["effect"], {"kind": "bonus_action_grant", "actions": ["dash", "disengage", "hide"]})
                self.assertIn("Thieves' Cant", d["languages"])
                self.assertNotIn("reactions", d)   # Uncanny Dodge stays descriptive
                for fid in DESCRIPTIVE["rogue"]:
                    if fid in f:
                        self.assertNotIn("effect", f[fid], fid)

    def test_paladin(self):
        smite = {"kind": "on_event", "on": "hit", "choice": "ask", "when": [{"kind": "weapon", "is": "melee"}],
                 "effects": [{"kind": "spend", "slot": "any"},
                             {"kind": "extra_damage", "damage": [{"dice": "2d8", "type": "radiant"}],
                              "scale": {"per_slot_level": {"base_level": 1, "add_dice": "1d8"}}}]}
        for level in LEVELS:
            with self.subTest(level=level):
                d = load("paladin", level)
                f = features(d)
                self.assertEqual(d["resources"], [{"id": "lay_on_hands", "name": "Lay on Hands", "max": {"per_level": 5, "class": "paladin"},
                                                   "recharge": "long_rest"}])
                self.assertEqual(f["lay_on_hands"]["effect"], {"kind": "heal_pool", "cost": "action", "pool": "lay_on_hands"})
                self.assertEqual("divine_smite" in f, level >= 2)
                self.assertEqual("fighting_style_dueling" in f, level >= 2)
                if level >= 2:
                    self.assertEqual(f["divine_smite"]["effect"], smite)
                    self.assertEqual(f["fighting_style_dueling"]["effect"],
                                     {"kind": "damage_bonus", "amount": 2, "condition": "one_handed_melee_no_other_weapon"})
                self.assertEqual("extra_attack" in f, level >= 5)
                if level >= 5:
                    self.assertEqual(f["extra_attack"]["effect"], {"kind": "extra_attack", "attacks_per_action": 2})
                for fid in DESCRIPTIVE["paladin"]:
                    if fid in f:
                        self.assertNotIn("effect", f[fid], fid)

    def test_ranger(self):
        for level in LEVELS:
            with self.subTest(level=level):
                d = load("ranger", level)
                f = features(d)
                row = TABLE["ranger"]["levels"][str(level)]
                self.assertEqual("fighting_style_archery" in f, level >= 2)
                if level >= 2:   # base to_hit stays unbaked (test_attacks_from_items); the +2 is the feature
                    self.assertEqual(f["fighting_style_archery"]["effect"], {"kind": "attack_bonus", "attacks": "ranged", "amount": 2})
                    self.assertLessEqual(len(d["spellcasting"]["spells"]), row["spells_known"])
                self.assertEqual("extra_attack" in f, level >= 5)
                if level >= 5:
                    self.assertEqual(f["extra_attack"]["effect"], {"kind": "extra_attack", "attacks_per_action": 2})
                self.assertNotIn("resources", d)
                for fid in DESCRIPTIVE["ranger"]:
                    if fid in f:
                        self.assertNotIn("effect", f[fid], fid)

    def test_barbarian(self):
        for level in LEVELS:
            with self.subTest(level=level):
                d = load("barbarian", level)
                f = features(d)
                self.assertFalse([e for e in d["equipment"] if isinstance(e, dict) and item(e["item"])["category"] == "armor"])
                self.assertEqual("reckless_attack" in f, level >= 2)
                if level >= 2:
                    self.assertEqual(f["reckless_attack"]["effect"], {"kind": "reckless_attack"})
                self.assertEqual("extra_attack" in f, level >= 5)
                if level >= 5:
                    self.assertEqual(f["extra_attack"]["effect"], {"kind": "extra_attack", "attacks_per_action": 2})
                self.assertNotIn("resources", d)   # no rage pool: nothing loaded would spend it (check 12)
                for fid in DESCRIPTIVE["barbarian"]:
                    if fid in f:
                        self.assertNotIn("effect", f[fid], fid)

    def test_bard(self):
        for level in LEVELS:
            with self.subTest(level=level):
                d = load("bard", level)
                f = features(d)
                row = TABLE["bard"]["levels"][str(level)]
                die = f"1d{row['class_specific']['bardic_inspiration_die']}"
                self.assertLessEqual(len(d["spellcasting"]["spells"]), row["spells_known"] + row["cantrips_known"])
                if level >= 3:   # the pool exists only where something spends it (check 12): Cutting Words
                    self.assertEqual(d["resources"], [{"id": "bardic_inspiration", "name": "Bardic Inspiration",
                                                       "max": {"plus_ability": "cha", "min": 1},
                                                       "recharge": "short_rest" if level >= 5 else "long_rest"}])   # Font of Inspiration
                    cw = d["reactions"][0]
                    self.assertEqual((cw["id"], cw["window"], cw["effect"], cw["usage"]),
                                     ("cutting_words", {"kind": "roll_attack"}, {"kind": "subtract_roll", "dice": die},
                                      {"kind": "pool", "pool": "bardic_inspiration", "cost": 1}))
                else:
                    self.assertNotIn("resources", d)
                    self.assertNotIn("reactions", d)
                self.assertEqual("font_of_inspiration" in f, level >= 5)
                for fid in DESCRIPTIVE["bard"]:
                    if fid in f:
                        self.assertNotIn("effect", f[fid], fid)

    def test_arcane_recovery_slot_recovery(self):
        # VERSION_BUMP_BATCH 7.2n: budget ceil(wizard level / 2) is derived from classes[], not authored.
        for level in LEVELS:
            with self.subTest(level=level):
                f = features(load("wizard", level))["arcane_recovery"]
                self.assertEqual(f["effect"], {"kind": "slot_recovery", "class": "wizard", "max_slot_level": 5, "uses": 1})
                self.assertIn("half your wizard level (rounded up)", f["description"])

    def test_authored_and_deferred_split(self):
        doc = (ROOT / "docs/SRD_CLASSES_DEFERRED.md").read_text(encoding="utf-8")
        for cls in TABLE:
            for level in LEVELS:
                row = TABLE[cls]["levels"][str(level)]
                d = load(cls, level)
                f = features(d)
                for idx in list(row["features"]) + list(row.get("subclass_features", {})):
                    with self.subTest(cls=cls, level=level, feature=idx):
                        self.assertTrue((idx in AUTHORED) != (idx in DEFERRED), "classify every feature exactly once")
                        where = AUTHORED.get(idx)
                        if where and where.startswith("feature:"):
                            fid = where.split(":", 1)[1]
                            for later in range(level, 6):   # a feature, once gained, stays
                                self.assertIn(fid, features(load(cls, later)))
                        elif where == "subclass":
                            self.assertIn("subclass", d["classes"][0])
                        elif where == "spellcasting":
                            self.assertIn("spellcasting", d)
                        elif where == "reactions":
                            self.assertTrue(d.get("reactions"))
                        elif where == "abilities":
                            pass   # pinned by test_abilities_and_asi
                        else:
                            self.assertIn(idx, DEFERRED)
                            self.assertIn(f"`{idx}`", doc)
                own = {i for lv in TABLE[cls]["levels"].values() for i in list(lv["features"]) + list(lv.get("subclass_features", {}))}
                for idx, fid in DEFERRED.items():   # deferred, not approximated (ids are per class: the paladin has its own channel_divinity)
                    if idx in own:
                        self.assertNotIn(fid, f)
        for s in DEFERRED_SPELLS:
            self.assertIn(f"srd51.spell.{s.replace('-', '_')}", doc)
            self.assertFalse((PACK / "spells" / f"{s.replace('-', '_')}.json").exists(), s)

    def test_manifest_and_ids(self):
        man = json.loads((PACK / "manifest.json").read_text(encoding="utf-8"))
        have = sorted(p.name for p in (PACK / "characters").glob("*.json"))
        self.assertEqual(have, sorted(f"{c}_level_{n}.json" for c in TABLE for n in LEVELS))
        for p in have:
            self.assertIn(f"characters/{p}", man["contents"])
        self.assertEqual(man["contents"], sorted(man["contents"]))
        self.assertEqual(man["api_level"], 2)
        # no id collides with the sample pack (which depends on srd51.core and ships srd51.character.fighter_1)
        sample = {json.loads(p.read_text(encoding="utf-8"))["id"]
                  for p in (ROOT / "content/packs/sample.vertical_slice/characters").glob("*.json")}
        self.assertFalse(sample & {d["id"] for _c, _l, d in docs()})


if __name__ == "__main__":
    unittest.main()
